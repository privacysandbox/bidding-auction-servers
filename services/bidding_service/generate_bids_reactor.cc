//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/bidding_service/generate_bids_reactor.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/message_differencer.h>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "services/bidding_service/bidding_v8_constants.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/common/constants/common_constants.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/error_categories.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::google::protobuf::TextFormat;

using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using IGForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;
constexpr int kArgsSizeWithWrapper = 7;

absl::StatusOr<std::string> ProtoToJson(
    const google::protobuf::Message& proto) {
  auto options = google::protobuf::util::JsonPrintOptions();
  std::string json;
  PS_RETURN_IF_ERROR(
      google::protobuf::util::MessageToJsonString(proto, &json, options));
  return json;
}

constexpr char kTopWindowHostname[] = "topWindowHostname";
constexpr char kSeller[] = "seller";
constexpr char kTopLevelSeller[] = "topLevelSeller";
constexpr char kJoinCount[] = "joinCount";
constexpr char kBidCount[] = "bidCount";
constexpr char kRecency[] = "recency";
constexpr char kMultiBidLimit[] = "multiBidLimit";
constexpr char kPrevWins[] = "prevWins";
constexpr char kPrevWinsMs[] = "prevWinsMs";
constexpr char kForDebuggingOnlyInCooldownOrLockout[] =
    "forDebuggingOnlyInCooldownOrLockout";
constexpr char kJsonStringEnd[] = R"JSON(",")JSON";
constexpr char kJsonStringValueStart[] = R"JSON(":")JSON";
constexpr char kJsonValueStart[] = R"JSON(":)JSON";
constexpr char kJsonValueEnd[] = R"JSON(,")JSON";
constexpr char kJsonEmptyString[] = R"JSON("")JSON";
constexpr char kEmptyDeviceSignals[] = R"JSON({})JSON";

std::string MakeBrowserSignalsForScript(
    absl::string_view publisher_name, absl::string_view seller,
    absl::string_view top_level_seller,
    const BrowserSignalsForBidding& browser_signals, uint32_t data_version,
    int32_t multi_bid_limit, const ForDebuggingOnlyFlags& fdo_flags) {
  std::string device_signals_str = R"JSON({")JSON";
  absl::StrAppend(&device_signals_str, kTopWindowHostname,
                  kJsonStringValueStart, publisher_name, kJsonStringEnd);
  absl::StrAppend(&device_signals_str, kSeller, kJsonStringValueStart, seller,
                  kJsonStringEnd);
  if (!top_level_seller.empty()) {
    absl::StrAppend(&device_signals_str, kTopLevelSeller, kJsonStringValueStart,
                    top_level_seller, kJsonStringEnd);
  }
  absl::StrAppend(&device_signals_str, kJoinCount, kJsonValueStart,
                  browser_signals.join_count(), kJsonValueEnd);
  absl::StrAppend(&device_signals_str, kBidCount, kJsonValueStart,
                  browser_signals.bid_count(), kJsonValueEnd);
  // recency is expected to be in milli seconds.
  int64_t recency_ms;
  if (browser_signals.has_recency_ms()) {
    recency_ms = browser_signals.recency_ms();
  } else {
    recency_ms = browser_signals.recency() * 1000;
  }
  absl::StrAppend(&device_signals_str, kRecency, kJsonValueStart, recency_ms,
                  kJsonValueEnd);
  // TODO(b/394397742): Deprecate prevWins in favor of prevWinsMs.
  absl::StrAppend(
      &device_signals_str, kPrevWins, kJsonValueStart,
      browser_signals.prev_wins().empty() ? kJsonEmptyString
                                          : browser_signals.prev_wins(),
      kJsonValueEnd, kPrevWinsMs, kJsonValueStart,
      browser_signals.prev_wins_ms().empty() ? kJsonEmptyString
                                             : browser_signals.prev_wins_ms(),
      kJsonValueEnd);
  absl::StrAppend(&device_signals_str, kDataVersion, kJsonValueStart,
                  data_version, kJsonValueEnd);
  absl::StrAppend(&device_signals_str, kForDebuggingOnlyInCooldownOrLockout,
                  kJsonValueStart,
                  fdo_flags.in_cooldown_or_lockout() ? "true" : "false",
                  kJsonValueEnd);
  absl::StrAppend(&device_signals_str, kMultiBidLimit, kJsonValueStart,
                  multi_bid_limit > 0 ? multi_bid_limit : kDefaultMultiBidLimit,
                  "}");
  return device_signals_str;
}

absl::StatusOr<std::string> SerializeRepeatedStringField(
    const google::protobuf::RepeatedPtrField<std::string>&
        repeated_string_field) {
  rapidjson::Document json_array;
  json_array.SetArray();
  for (const auto& item : repeated_string_field) {
    json_array.PushBack(
        rapidjson::Value(item.c_str(), json_array.GetAllocator()).Move(),
        json_array.GetAllocator());
  }
  return SerializeJsonDoc(json_array);
}

constexpr char kName[] = "name";
constexpr char kTrustedBiddingSignalsKeys[] = "trustedBiddingSignalsKeys";
constexpr char kAdRenderIds[] = "adRenderIds";
constexpr char kAdComponentRenderIds[] = "adComponentRenderIds";
constexpr char kUserBiddingSignals[] = "userBiddingSignals";

// Manual/Custom serializer for Interest Group.
// Empty fields will not be included at all in the serialized JSON.
// No default, null, or dummy values are filled in.
// Device signals are not serialized since they are passed to generateBid()
// in a different parameter.
absl::StatusOr<std::string> SerializeIG(const IGForBidding& ig) {
  // Insert the name in quotes.
  std::string serialized_ig =
      absl::StrFormat(R"JSON({"%s":"%s")JSON", kName, ig.name());
  // Bidding signals keys.
  if (!ig.trusted_bidding_signals_keys().empty()) {
    absl::StatusOr<std::string> bidding_signals_keys_json_arr =
        SerializeRepeatedStringField(ig.trusted_bidding_signals_keys());
    if (!bidding_signals_keys_json_arr.ok()) {
      return bidding_signals_keys_json_arr.status();
    }
    absl::StrAppend(
        &serialized_ig,
        absl::StrFormat(R"JSON(,"%s":%s)JSON", kTrustedBiddingSignalsKeys,
                        bidding_signals_keys_json_arr.value()));
  }
  // Ad render IDs.
  if (!ig.ad_render_ids().empty()) {
    PS_ASSIGN_OR_RETURN(std::string ad_render_ids_json_arr,
                        SerializeRepeatedStringField(ig.ad_render_ids()));
    absl::StrAppend(&serialized_ig,
                    absl::StrFormat(R"JSON(,"%s":%s)JSON", kAdRenderIds,
                                    ad_render_ids_json_arr));
  }
  // Ad component render IDs.
  if (!ig.ad_component_render_ids().empty()) {
    PS_ASSIGN_OR_RETURN(
        absl::StatusOr<std::string> ad_component_render_ids_json_arr,
        SerializeRepeatedStringField(ig.ad_component_render_ids()));
    absl::StrAppend(
        &serialized_ig,
        absl::StrFormat(R"JSON(,"%s":%s)JSON", kAdComponentRenderIds,
                        ad_component_render_ids_json_arr.value()));
  }
  // User Bidding Signals gets no quotes as it needs no transformation.
  if (!ig.user_bidding_signals().empty()) {
    absl::StrAppend(&serialized_ig,
                    absl::StrFormat(R"JSON(,"%s":%s)JSON", kUserBiddingSignals,
                                    ig.user_bidding_signals()));
  }
  // Device signals do not have to be serialized.
  absl::StrAppend(&serialized_ig, "}");
  return serialized_ig;
}

// Builds a vector containing commonly shared inputs, following the description
// here:
// https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_api.md#generatebids
//
// raw_request: The raw request used to generate inputs.
// return: a vector of shared ptrs to strings. Fields (indices) that are not
// shared by all dispatch requests are left empty, to be  filled in with values
// specific to the request.
std::vector<std::shared_ptr<std::string>> BuildBaseInput(
    const RawRequest& raw_request) {
  int args_size = kArgsSizeWithWrapper;

  std::vector<std::shared_ptr<std::string>> input(
      args_size, std::make_shared<std::string>());  // GenerateBidUdfArgs size.
  input[ArgIndex(GenerateBidUdfArgs::kAuctionSignals)] =
      std::make_shared<std::string>((raw_request.auction_signals().empty())
                                        ? "\"\""
                                        : raw_request.auction_signals());
  input[ArgIndex(GenerateBidUdfArgs::kBuyerSignals)] =
      std::make_shared<std::string>((raw_request.buyer_signals().empty())
                                        ? "\"\""
                                        : raw_request.buyer_signals());
  return input;
}

// Builds a Dispatch Request for the ROMA Engine for a single Interest Group.
absl::StatusOr<DispatchRequest> BuildGenerateBidRequest(
    IGForBidding& interest_group, const RawRequest& raw_request,
    const std::vector<std::shared_ptr<std::string>>& base_input,
    const bool enable_buyer_debug_url_generation,
    RequestLogContext& log_context, const bool enable_adtech_code_logging,
    absl::string_view version) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest generate_bid_request;
  generate_bid_request.id = interest_group.name();
  generate_bid_request.version_string = version;
  // Copy base input and amend with custom interest_group
  generate_bid_request.input = base_input;

  generate_bid_request
      .input[ArgIndex(GenerateBidUdfArgs::kTrustedBiddingSignals)] =
      std::make_shared<std::string>(
          std::move(*interest_group.mutable_trusted_bidding_signals()));
  interest_group.clear_trusted_bidding_signals();

  // IG must have device signals to participate in Bidding.
  google::protobuf::util::MessageDifferencer differencer;
  if (interest_group.has_browser_signals_for_bidding() &&
      interest_group.browser_signals_for_bidding().IsInitialized() &&
      !differencer.Equals(BrowserSignalsForBidding::default_instance(),
                          interest_group.browser_signals_for_bidding())) {
    generate_bid_request.input[ArgIndex(GenerateBidUdfArgs::kDeviceSignals)] =
        std::make_shared<std::string>(MakeBrowserSignalsForScript(
            raw_request.publisher_name(), raw_request.seller(),
            raw_request.top_level_seller(),
            interest_group.browser_signals_for_bidding(),
            raw_request.data_version(), raw_request.multi_bid_limit(),
            raw_request.fdo_flags()));
  } else if (interest_group.has_android_signals_for_bidding() &&
             interest_group.android_signals_for_bidding().IsInitialized() &&
             !differencer.Equals(
                 AndroidSignalsForBidding::default_instance(),
                 interest_group.android_signals_for_bidding())) {
    PS_ASSIGN_OR_RETURN(
        std::string serialized_android_signals,
        ProtoToJson(interest_group.android_signals_for_bidding()));
    generate_bid_request.input[ArgIndex(GenerateBidUdfArgs::kDeviceSignals)] =
        std::make_shared<std::string>((serialized_android_signals.empty())
                                          ? kEmptyDeviceSignals
                                          : serialized_android_signals);
  } else {
    generate_bid_request.input[ArgIndex(GenerateBidUdfArgs::kDeviceSignals)] =
        std::make_shared<std::string>(kEmptyDeviceSignals);
  }
  generate_bid_request.input[ArgIndex(GenerateBidUdfArgs::kMultiBidLimit)] =
      std::make_shared<std::string>(
          absl::StrCat(raw_request.multi_bid_limit()));

  // Debug urls are collected from generateBid() only when:
  // i) The server has enabled debug url generation, and
  // ii) The request has enabled debug reporting, and
  // iii) The buyer is not in cooldown or lockout if downsampling is enabled.
  bool enable_debug_reporting =
      enable_buyer_debug_url_generation &&
      raw_request.enable_debug_reporting() &&
      !(raw_request.fdo_flags().enable_sampled_debug_reporting() &&
        raw_request.fdo_flags().in_cooldown_or_lockout());
  generate_bid_request.input[ArgIndex(GenerateBidUdfArgs::kFeatureFlags)] =
      std::make_shared<std::string>(GetFeatureFlagJson(
          enable_adtech_code_logging, enable_debug_reporting));

  generate_bid_request.handler_name =
      kDispatchHandlerFunctionNameWithCodeWrapper;

  auto start_parse_time = absl::Now();
  auto serialized_ig = SerializeIG(interest_group);
  if (!serialized_ig.ok()) {
    return serialized_ig.status();
  }
  generate_bid_request.input[ArgIndex(GenerateBidUdfArgs::kInterestGroup)] =
      std::make_shared<std::string>(std::move(serialized_ig.value()));
  PS_VLOG(kStats, log_context)
      << "\nInterest Group Serialize Time: "
      << ToInt64Microseconds((absl::Now() - start_parse_time))
      << " microseconds for "
      << generate_bid_request
             .input[ArgIndex(GenerateBidUdfArgs::kInterestGroup)]
             ->size()
      << " bytes.";

  if (server_common::log::PS_VLOG_IS_ON(10)) {
    PS_VLOG(10, log_context) << "\n\nGenerateBid Input Args:";
    for (const auto& it : generate_bid_request.input) {
      PS_VLOG(10, log_context) << *it;
    }
  }
  return generate_bid_request;
}

// Removes contributions with no event type and contributions beyond
// per_adtech_paapi_contributions_limit for each event type.
void ProcessPAggContributions(AdWithBid& bid,
                              int per_adtech_paapi_contributions_limit) {
  absl::flat_hash_map<EventType, int> event_type_counts;
  std::vector<int> indices_to_remove;

  for (int i = 0; i < bid.private_aggregation_contributions_size(); ++i) {
    const auto& contribution = bid.private_aggregation_contributions(i);
    EventType event_type = contribution.event().event_type();
    if (event_type == EVENT_TYPE_UNSPECIFIED ||
        event_type_counts[event_type] >= per_adtech_paapi_contributions_limit) {
      indices_to_remove.push_back(i);
    } else {
      event_type_counts[event_type]++;
    }
  }

  // Remove elements in reverse order to avoid index invalidation
  for (auto it = indices_to_remove.rbegin(); it != indices_to_remove.rend();
       ++it) {
    bid.mutable_private_aggregation_contributions()->erase(
        bid.mutable_private_aggregation_contributions()->begin() + *it);
  }
}

template <typename T>
void UpdateMaxMetric(const absl::flat_hash_map<std::string, double>& metrics,
                     const std::string& key, T& value) {
  auto it = metrics.find(key);
  if (it != metrics.end()) {
    value = std::max(value, it->second);
  }
}

void LogRomaMetrics(const std::vector<absl::StatusOr<DispatchResponse>>& result,
                    std::unique_ptr<metric::BiddingContext>& metric_context) {
  double execution_duration_ms = 0;
  double max_queueing_duration_ms = 0;
  double code_run_duration_ms = 0;
  double json_input_parsing_duration_ms = 0;
  double js_engine_handler_call_duration_ms = 0;
  double queue_fullness_ratio = 0;
  double active_worker_ratio = 0;

  for (const auto& res : result) {
    if (res.ok()) {
      const auto& metrics = res.value().metrics;
      UpdateMaxMetric(metrics, metric::kRawRomaExecutionDuration,
                      execution_duration_ms);
      UpdateMaxMetric(metrics, metric::kRawRomaExecutionQueueFullnessRatio,
                      queue_fullness_ratio);
      UpdateMaxMetric(metrics, metric::kRawRomaExecutionActiveWorkerRatio,
                      active_worker_ratio);

      UpdateMaxMetric(metrics, metric::kRawRomaExecutionWaitTime,
                      max_queueing_duration_ms);
      UpdateMaxMetric(metrics, metric::kRawRomaExecutionCodeRunDuration,
                      code_run_duration_ms);
      UpdateMaxMetric(metrics,
                      metric::kRawRomaExecutionJsonInputParsingDuration,
                      json_input_parsing_duration_ms);
      UpdateMaxMetric(metrics,
                      metric::kRawRomaExecutionJsEngineHandlerCallDuration,
                      js_engine_handler_call_duration_ms);
    }
  }
  LogIfError(metric_context->LogHistogram<metric::kRomaExecutionDuration>(
      static_cast<int>(execution_duration_ms)));
  LogIfError(
      metric_context->LogHistogram<metric::kRomaExecutionQueueFullnessRatio>(
          queue_fullness_ratio));
  LogIfError(
      metric_context->LogHistogram<metric::kRomaExecutionActiveWorkerRatio>(
          active_worker_ratio));
  LogIfError(
      metric_context->LogHistogram<metric::kUdfExecutionQueueingDuration>(
          static_cast<int>(max_queueing_duration_ms)));
  LogIfError(
      metric_context->LogHistogram<metric::kRomaExecutionCodeRunDuration>(
          static_cast<int>(code_run_duration_ms)));
  LogIfError(metric_context
                 ->LogHistogram<metric::kRomaExecutionJsonInputParsingDuration>(
                     static_cast<int>(json_input_parsing_duration_ms)));
  LogIfError(
      metric_context
          ->LogHistogram<metric::kRomaExecutionJsEngineHandlerCallDuration>(
              static_cast<int>(js_engine_handler_call_duration_ms)));
}

}  // namespace

GenerateBidsReactor::GenerateBidsReactor(
    grpc::CallbackServerContext* context, V8DispatchClient& dispatcher,
    const GenerateBidsRequest* request, GenerateBidsResponse* response,
    std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BiddingServiceRuntimeConfig& runtime_config)
    : BaseGenerateBidsReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse>(
          runtime_config, request, response, key_fetcher_manager,
          crypto_client),
      context_(context),
      dispatcher_(dispatcher),
      benchmarking_logger_(std::move(benchmarking_logger)),
      auction_scope_(
          raw_request_.top_level_seller().empty()
              ? AuctionScope::AUCTION_SCOPE_SINGLE_SELLER
              // Device and server component auctions have the same flow for
              // PA Auctions.
              : AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER),
      per_adtech_paapi_contributions_limit_(
          runtime_config.per_adtech_paapi_contributions_limit) {
  PS_CHECK_OK(
      [this]() {
        PS_ASSIGN_OR_RETURN(metric_context_,
                            metric::BiddingContextMap()->Remove(request_));
        if (log_context_.is_consented()) {
          metric_context_->SetConsented(
              raw_request_.log_context().generation_id());
        } else if (log_context_.is_prod_debug()) {
          metric_context_->SetConsented(kProdDebug.data());
        }
        return absl::OkStatus();
      }(),
      log_context_)
      << "BiddingContextMap()->Get(request) should have been called";
  if (runtime_config.use_per_request_udf_versioning) {
    protected_audience_generate_bid_version_ =
        raw_request_.blob_versions().protected_audience_generate_bid_udf();
  }
  if (protected_audience_generate_bid_version_.empty()) {
    protected_audience_generate_bid_version_ =
        runtime_config.default_protected_audience_generate_bid_version;
  }
  debug_urls_validation_config_ = {
      .max_allowed_size_debug_url_chars =
          runtime_config.max_allowed_size_debug_url_bytes,
      .max_allowed_size_all_debug_urls_chars =
          kBytesMultiplyer * runtime_config.max_allowed_size_all_debug_urls_kb,
      .enable_sampled_debug_reporting =
          raw_request_.fdo_flags().enable_sampled_debug_reporting(),
      .debug_reporting_sampling_upper_bound =
          runtime_config.debug_reporting_sampling_upper_bound};
}

void GenerateBidsReactor::Execute() {
  if (enable_cancellation_ && context_->IsCancelled()) {
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }
  benchmarking_logger_->BuildInputBegin();
  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    if (server_common::log::PS_VLOG_IS_ON(kEncrypted)) {
      PS_VLOG(kEncrypted, log_context_)
          << "Encrypted GenerateBidsRequest exported in EventMessage if "
             "consented";
      log_context_.SetEventMessageField(*request_);
    }
    PS_VLOG(kPlain, log_context_)
        << "GenerateBidsRawRequest exported in EventMessage if consented";
    log_context_.SetEventMessageField(raw_request_);
  }

  auto interest_groups = raw_request_.interest_group_for_bidding();
  if (interest_groups.empty()) {
    // This is unlikely to happen since we already have this check in place
    // in BFE.
    PS_LOG(ERROR, log_context_) << "Request has no interest groups.";
    EncryptResponseAndFinish(
        server_common::FromAbslStatus(absl::InvalidArgumentError(
            "Request must have at least one interest group.")));
    return;
  }

  // Build base input.
  std::vector<std::shared_ptr<std::string>> base_input =
      BuildBaseInput(raw_request_);
  for (int i = 0; i < interest_groups.size(); i++) {
    absl::StatusOr<DispatchRequest> generate_bid_request =
        BuildGenerateBidRequest(interest_groups.at(i), raw_request_, base_input,
                                enable_buyer_debug_url_generation_,
                                log_context_, enable_adtech_code_logging_,
                                protected_audience_generate_bid_version_);
    if (!generate_bid_request.ok()) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Unable to build GenerateBidRequest: "
          << generate_bid_request.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    } else {
      auto dispatch_request = generate_bid_request.value();
      dispatch_request.metadata =
          RomaSharedContextWithMetric<google::protobuf::Message>(
              request_, roma_request_context_factory_.Create(), log_context_);
      dispatch_request.tags[kRomaTimeoutTag] = roma_timeout_ms_;
      dispatch_requests_.push_back(dispatch_request);
    }
  }

  if (dispatch_requests_.empty()) {
    EncryptResponseAndFinish(grpc::Status::OK);
    return;
  }

  benchmarking_logger_->BuildInputEnd();
  absl::Time start_js_execution_time = absl::Now();

  auto status = dispatcher_.BatchExecute(
      dispatch_requests_,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this, start_js_execution_time](
              const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            int js_execution_time_ms =
                (absl::Now() - start_js_execution_time) / absl::Milliseconds(1);
            LogIfError(
                metric_context_->LogHistogram<metric::kUdfExecutionDuration>(
                    js_execution_time_ms));
            GenerateBidsCallback(result);
            LogRomaMetrics(result, metric_context_);
            EncryptResponseAndFinish(grpc::Status::OK);
          },
          [this]() {
            EncryptResponseAndFinish(
                grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
          }));
  int dispatcher_initialization_time_ms =
      (absl::Now() - start_js_execution_time) / absl::Milliseconds(1);
  LogIfError(
      metric_context_
          ->LogHistogram<metric::kUdfExecutionDispatcherInitializationDuration>(
              dispatcher_initialization_time_ms));

  if (!status.ok()) {
    LogIfError(
        metric_context_->LogHistogram<metric::kBiddingFailedToBidPercent>(1.0));
    LogIfError(
        metric_context_->LogHistogram<metric::kBiddingTotalBidsCount>(0));
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                       1, metric::kBiddingGenerateBidsFailedToDispatchCode));
    PS_LOG(ERROR, log_context_)
        << "Execution request failed for batch: " << raw_request_.DebugString()
        << status.ToString(absl::StatusToStringMode::kWithEverything);
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::INTERNAL, status.ToString()));
  }
}

// Handles the output of the code execution dispatch.
// Note that the dispatch response value is expected to be a json string
// conforming to the generateBid function output described here:
// https://github.com/WICG/turtledove/blob/main/FLEDGE.md#32-on-device-bidding
void GenerateBidsReactor::GenerateBidsCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& output) {
  if (server_common::log::PS_VLOG_IS_ON(kDispatch)) {
    for (const auto& dispatch_response : output) {
      PS_VLOG(kDispatch, log_context_)
          << "Generate Bids V8 Response: " << dispatch_response.status();
      if (dispatch_response.ok()) {
        PS_VLOG(kDispatch, log_context_) << dispatch_response.value().resp;
      }
    }
  }

  if (output.empty()) {
    PS_LOG(INFO, log_context_)
        << "Received empty response array from Generate Bids V8 response.";
    LogIfError(
        metric_context_->LogHistogram<metric::kBiddingFailedToBidPercent>(1.0));
    LogIfError(
        metric_context_->LogHistogram<metric::kBiddingTotalBidsCount>(0));
    return;
  }

  benchmarking_logger_->HandleResponseBegin();
  int failed_to_bid_count = 0;
  int received_bid_count = 0;
  int zero_bid_count = 0;
  int total_debug_urls_count = 0;
  long total_debug_urls_chars = 0;
  int rejected_component_bid_count = 0;

  // Iterate through every result in the output.
  for (int i = 0; i < output.size(); i++) {
    auto& result = output.at(i);
    if (!result.ok()) {
      failed_to_bid_count += 1;
      // Error result due to execution timeout.
      if (absl::StrContains(result.status().message(), "timeout")) {
        PS_LOG(ERROR, log_context_)
            << "Execution timed out: "
            << result.status().ToString(
                   absl::StatusToStringMode::kWithEverything);
        LogIfError(
            metric_context_
                ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                    1, metric::kBiddingGenerateBidsTimedOutError));
      } else {
        // Error result due to invalid execution.
        PS_LOG(ERROR, log_context_)
            << "Invalid execution (possibly invalid input): "
            << result.status().ToString(
                   absl::StatusToStringMode::kWithEverything);
        LogIfError(
            metric_context_
                ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                    1, metric::kBiddingGenerateBidsDispatchResponseError));
        LogIfError(
            metric_context_->AccumulateMetric<metric::kUdfExecutionErrorCount>(
                1));
      }
      continue;
    }

    // Parse JSON response from the result.
    absl::StatusOr<std::vector<std::string>> generate_bid_responses =
        ParseAndGetResponseJsonArray(enable_adtech_code_logging_, result->resp,
                                     log_context_);
    if (!generate_bid_responses.ok()) {
      failed_to_bid_count += 1;
      PS_LOG(ERROR, log_context_)
          << "Failed to parse response from Roma "
          << generate_bid_responses.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                         1, metric::kBiddingGenerateBidsDispatchResponseError));
      continue;
    }

    // Convert JSON response to AdWithBid proto.
    google::protobuf::json::ParseOptions parse_options;
    parse_options.ignore_unknown_fields = true;
    const std::string interest_group_name = result->id;
    int received_bid_count_for_current_response = 0;
    bool failed_to_parse_bid = false;
    for (const auto& bid_response : *generate_bid_responses) {
      AdWithBid bid;
      auto valid = google::protobuf::util::JsonStringToMessage(
          bid_response, &bid, parse_options);
      if (!valid.ok()) {
        PS_LOG(ERROR, log_context_)
            << "Invalid json output from code execution for interest_group "
            << interest_group_name << ": " << bid_response;
        failed_to_parse_bid = true;
        continue;
      }

      if (bid.ad().has_string_value()) {
        // Escape string so that this can be properly processed by the V8 engine
        // in Auction service.
        bid.mutable_ad()->set_string_value(
            absl::StrCat("\"", bid.ad().string_value(), "\""));
      }
      received_bid_count_for_current_response += 1;

      // Validate AdWithBid proto.
      if (absl::Status validation_status =
              IsValidProtectedAudienceBid(bid, auction_scope_);
          !validation_status.ok()) {
        PS_VLOG(kNoisyWarn, log_context_) << validation_status.message();
        if (validation_status.code() == absl::StatusCode::kInvalidArgument) {
          zero_bid_count += 1;
        } else if (validation_status.code() ==
                   absl::StatusCode::kPermissionDenied) {
          // Received allowComponentAuction=false.
          ++rejected_component_bid_count;
        }
        continue;
      }

      // Validate debug URLs and Private Aggregation contributions.
      total_debug_urls_count += ValidateBuyerDebugUrls(
          bid, total_debug_urls_chars, debug_urls_validation_config_,
          log_context_, metric_context_.get());
      ProcessPAggContributions(bid, per_adtech_paapi_contributions_limit_);

      // Add the AdWithBid to GenerateBidsResponse.
      bid.set_data_version(raw_request_.data_version());
      bid.set_interest_group_name(interest_group_name);
      *raw_response_.add_bids() = std::move(bid);
    }

    if (failed_to_parse_bid) {
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                         1, metric::kBiddingGenerateBidsDispatchResponseError));
    }
    if (received_bid_count_for_current_response == 0) {
      failed_to_bid_count += 1;
    }
    received_bid_count += received_bid_count_for_current_response;
  }

  LogIfError(metric_context_->LogHistogram<metric::kBiddingFailedToBidPercent>(
      (static_cast<double>(failed_to_bid_count)) / output.size()));
  LogIfError(metric_context_->LogHistogram<metric::kBiddingTotalBidsCount>(
      received_bid_count));
  if (received_bid_count > 0) {
    LogIfError(metric_context_->LogUpDownCounter<metric::kBiddingZeroBidCount>(
        zero_bid_count));
    LogIfError(metric_context_->LogHistogram<metric::kBiddingZeroBidPercent>(
        (static_cast<double>(zero_bid_count)) / received_bid_count));
  }
  if (rejected_component_bid_count > 0) {
    LogIfError(
        metric_context_->LogUpDownCounter<metric::kBiddingBidRejectedCount>(
            rejected_component_bid_count));
  }
  LogIfError(metric_context_->AccumulateMetric<metric::kBiddingDebugUrlCount>(
      total_debug_urls_count, kBuyerDebugUrlSentToSeller));
  LogIfError(metric_context_->LogHistogram<metric::kBiddingDebugUrlsSizeBytes>(
      static_cast<double>(total_debug_urls_chars)));
  benchmarking_logger_->HandleResponseEnd();
}

void GenerateBidsReactor::EncryptResponseAndFinish(grpc::Status status) {
  raw_response_.set_bidding_export_debug(log_context_.ShouldExportEvent());
  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    PS_VLOG(kPlain, log_context_)
        << "GenerateBidsRawResponse exported in EventMessage if consented";
    log_context_.SetEventMessageField(raw_response_);
  }
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(/*if_export_consented=*/true,
                                  log_context_.ShouldExportEvent());

  if (!EncryptResponse()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to encrypt the generate bids response.";
    status = grpc::Status(grpc::INTERNAL, kInternalServerError);
  }
  if (status.error_code() != grpc::StatusCode::OK) {
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  }
  PS_VLOG(kEncrypted, log_context_) << "Encrypted GenerateBidsResponse\n"
                                    << response_->ShortDebugString();
  Finish(status);
}

void GenerateBidsReactor::OnDone() { delete this; }

}  // namespace privacy_sandbox::bidding_auction_servers
