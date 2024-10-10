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

#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/message_differencer.h>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/constants.h"
#include "services/bidding_service/utils/validation.h"
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
constexpr int kArgsSizeWithWrapper = 6;

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
constexpr char kPrevWins[] = "prevWins";
constexpr char kJsonStringEnd[] = R"JSON(",")JSON";
constexpr char kJsonStringValueStart[] = R"JSON(":")JSON";
constexpr char kJsonValueStart[] = R"JSON(":)JSON";
constexpr char kJsonValueEnd[] = R"JSON(,")JSON";
constexpr char kJsonEmptyString[] = R"JSON("")JSON";
constexpr char kEmptyDeviceSignals[] = R"JSON({})JSON";

std::string MakeBrowserSignalsForScript(absl::string_view publisher_name,
                                        absl::string_view seller,
                                        absl::string_view top_level_seller,
                                        const BrowserSignals& browser_signals) {
  std::string device_signals_str = absl::StrCat(
      R"JSON({")JSON", kTopWindowHostname, kJsonStringValueStart,
      publisher_name, kJsonStringEnd, kSeller, kJsonStringValueStart, seller);
  if (!top_level_seller.empty()) {
    absl::StrAppend(&device_signals_str, kJsonStringEnd, kTopLevelSeller,
                    kJsonStringValueStart, top_level_seller);
  }
  int64_t recency_ms;
  if (browser_signals.has_recency_ms()) {
    recency_ms = browser_signals.recency_ms();
  } else {
    recency_ms = browser_signals.recency() * 1000;
  }
  absl::StrAppend(
      &device_signals_str, kJsonStringEnd, kJoinCount, kJsonValueStart,
      browser_signals.join_count(), kJsonValueEnd, kBidCount, kJsonValueStart,
      browser_signals.bid_count(), kJsonValueEnd, kRecency, kJsonValueStart,
      /*recency is expected to be in milli seconds.*/
      recency_ms, kJsonValueEnd, kPrevWins, kJsonValueStart,
      browser_signals.prev_wins().empty() ? kJsonEmptyString
                                          : browser_signals.prev_wins(),
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
      args_size, std::make_shared<std::string>());  // GenerateBidArgs size.
  input[ArgIndex(GenerateBidArgs::kAuctionSignals)] =
      std::make_shared<std::string>((raw_request.auction_signals().empty())
                                        ? "\"\""
                                        : raw_request.auction_signals());
  input[ArgIndex(GenerateBidArgs::kBuyerSignals)] =
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
    const std::string& version) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest generate_bid_request;
  generate_bid_request.id = interest_group.name();
  // TODO(b/258790164) Update after code is fetched periodically.
  generate_bid_request.version_string = version;
  // Copy base input and amend with custom interest_group
  generate_bid_request.input = base_input;

  generate_bid_request
      .input[ArgIndex(GenerateBidArgs::kTrustedBiddingSignals)] =
      std::make_shared<std::string>(
          std::move(*interest_group.mutable_trusted_bidding_signals()));
  interest_group.clear_trusted_bidding_signals();

  // IG must have device signals to participate in Bidding.
  google::protobuf::util::MessageDifferencer differencer;
  if (interest_group.has_browser_signals() &&
      interest_group.browser_signals().IsInitialized() &&
      !differencer.Equals(BrowserSignals::default_instance(),
                          interest_group.browser_signals())) {
    generate_bid_request.input[ArgIndex(GenerateBidArgs::kDeviceSignals)] =
        std::make_shared<std::string>(MakeBrowserSignalsForScript(
            raw_request.publisher_name(), raw_request.seller(),
            raw_request.top_level_seller(), interest_group.browser_signals()));
  } else if (interest_group.has_android_signals() &&
             interest_group.android_signals().IsInitialized() &&
             !differencer.Equals(AndroidSignals::default_instance(),
                                 interest_group.android_signals())) {
    PS_ASSIGN_OR_RETURN(std::string serialized_android_signals,
                        ProtoToJson(interest_group.android_signals()));
    generate_bid_request.input[ArgIndex(GenerateBidArgs::kDeviceSignals)] =
        std::make_shared<std::string>((serialized_android_signals.empty())
                                          ? kEmptyDeviceSignals
                                          : serialized_android_signals);
  } else {
    generate_bid_request.input[ArgIndex(GenerateBidArgs::kDeviceSignals)] =
        std::make_shared<std::string>(kEmptyDeviceSignals);
  }
  generate_bid_request.input[ArgIndex(GenerateBidArgs::kFeatureFlags)] =
      std::make_shared<std::string>(
          GetFeatureFlagJson(enable_adtech_code_logging,
                             enable_buyer_debug_url_generation &&
                                 raw_request.enable_debug_reporting()));
  generate_bid_request.handler_name =
      kDispatchHandlerFunctionNameWithCodeWrapper;

  auto start_parse_time = absl::Now();
  auto serialized_ig = SerializeIG(interest_group);
  if (!serialized_ig.ok()) {
    return serialized_ig.status();
  }
  generate_bid_request.input[ArgIndex(GenerateBidArgs::kInterestGroup)] =
      std::make_shared<std::string>(std::move(serialized_ig.value()));
  PS_VLOG(kStats, log_context)
      << "\nInterest Group Serialize Time: "
      << ToInt64Microseconds((absl::Now() - start_parse_time))
      << " microseconds for "
      << generate_bid_request.input[ArgIndex(GenerateBidArgs::kInterestGroup)]
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
              : AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER),
      protected_auction_generate_bid_version_(
          runtime_config.default_protected_auction_generate_bid_version) {
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::BiddingContextMap()->Remove(request_));
    if (log_context_.is_consented()) {
      metric_context_->SetConsented(raw_request_.log_context().generation_id());
    }
    return absl::OkStatus();
  }()) << "BiddingContextMap()->Get(request) should have been called";
}

void GenerateBidsReactor::Execute() {
  if (enable_cancellation_ && context_->IsCancelled()) {
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }
  benchmarking_logger_->BuildInputBegin();

  PS_VLOG(kEncrypted, log_context_)
      << "Encrypted GenerateBidsRequest exported in EventMessage";
  log_context_.SetEventMessageField(*request_);
  PS_VLOG(kPlain, log_context_)
      << "GenerateBidsRawRequest exported in EventMessage";
  log_context_.SetEventMessageField(raw_request_);

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
                                protected_auction_generate_bid_version_);
    if (!generate_bid_request.ok()) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Unable to build GenerateBidRequest: "
          << generate_bid_request.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    } else {
      // Create new metric context for Inference metrics.
      metric::MetricContextMap<google::protobuf::Message>()->Get(request_);
      // Make metric_context a unique_ptr by releasing the ownership of the
      // context from ContextMap.
      absl::StatusOr<std::unique_ptr<metric::BiddingContext>> metric_context =
          metric::MetricContextMap<google::protobuf::Message>()->Remove(
              request_);
      CHECK_OK(metric_context);
      auto dispatch_request = generate_bid_request.value();
      dispatch_request.tags[kTimeoutMs] = roma_timeout_ms_;
      RomaRequestSharedContextBidding shared_context =
          roma_request_context_factory_.Create();
      auto roma_shared_context = shared_context.GetRomaRequestContext();
      if (roma_shared_context.ok()) {
        std::shared_ptr<RomaRequestContextBidding> roma_request_context =
            roma_shared_context.value();
        roma_request_context->SetIsProtectedAudienceRequest(true);
        roma_request_context->SetMetricContext(
            std::move(metric_context.value()));
      } else {
        PS_LOG(ERROR, log_context_)
            << "Failed to retrieve RomaRequestContextBidding: "
            << roma_shared_context.status();
      }
      dispatch_request.metadata = shared_context;
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
                metric_context_->LogHistogram<metric::kJSExecutionDuration>(
                    js_execution_time_ms));
            GenerateBidsCallback(result);
            EncryptResponseAndFinish(grpc::Status::OK);
          },
          [this]() {
            EncryptResponseAndFinish(
                grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
          }));

  if (!status.ok()) {
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
  if (server_common::log::PS_VLOG_IS_ON(2)) {
    for (const auto& dispatch_response : output) {
      PS_VLOG(kDispatch, log_context_)
          << "Generate Bids V8 Response: " << dispatch_response.status();
      if (dispatch_response.ok()) {
        PS_VLOG(kDispatch, log_context_) << dispatch_response.value().resp;
      }
    }
  }
  int total_bid_count = static_cast<int>(output.size());
  if (total_bid_count == 0) {
    PS_LOG(INFO, log_context_)
        << "Received empty response array from Generate Bids V8 response.";
    // TODO(b/368624844): Add an EmptyResponse error code to
    // kBiddingErrorCountByErrorCount metric.
    return;
  }
  LogIfError(metric_context_->AccumulateMetric<metric::kBiddingTotalBidsCount>(
      total_bid_count));
  benchmarking_logger_->HandleResponseBegin();
  int zero_bid_count = 0;
  int failed_requests = 0;
  long total_debug_urls_chars = 0;
  // Iterate through every result in the output.
  for (int i = 0; i < output.size(); i++) {
    auto& result = output.at(i);
    // Error result due to invalid execution.
    if (!result.ok()) {
      failed_requests = failed_requests + 1;
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                         1, metric::kBiddingGenerateBidsDispatchResponseError));
      PS_LOG(ERROR, log_context_)
          << "Invalid execution (possibly invalid input): "
          << result.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
      LogIfError(
          metric_context_->LogUpDownCounter<metric::kJSExecutionErrorCount>(1));
      continue;
    }
    // Parse JSON response from the result.
    AdWithBid bid;
    absl::StatusOr<std::string> generate_bid_response =
        enable_adtech_code_logging_
            ? ParseAndGetResponseJson(enable_adtech_code_logging_, result->resp,
                                      log_context_)
            : result->resp;
    if (!generate_bid_response.ok()) {
      PS_LOG(ERROR, log_context_)
          << "Failed to parse response from Roma "
          << generate_bid_response.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    }
    // Convert JSON response to AdWithBid proto.
    google::protobuf::json::ParseOptions parse_options;
    parse_options.ignore_unknown_fields = true;
    auto valid = google::protobuf::util::JsonStringToMessage(
        generate_bid_response.value(), &bid, parse_options);
    const std::string interest_group_name = result->id;
    if (!valid.ok()) {
      PS_LOG(ERROR, log_context_)
          << "Invalid json output from code execution for interest_group "
          << interest_group_name << ": " << result->resp;
      continue;
    }
    // Validate AdWithBid proto.
    if (absl::Status validation_status =
            IsValidProtectedAudienceBid(bid, auction_scope_);
        !validation_status.ok()) {
      PS_VLOG(kNoisyWarn, log_context_) << validation_status.message();
      if (validation_status.code() == absl::StatusCode::kInvalidArgument) {
        zero_bid_count += 1;
        LogIfError(
            metric_context_->AccumulateMetric<metric::kBiddingZeroBidCount>(1));
      }
      continue;
    }
    // Trim debug URLs for validated AdWithBid proto and add it to
    // GenerateBidsResponse.
    total_debug_urls_chars +=
        TrimAndReturnDebugUrlsSize(bid, max_allowed_size_debug_url_chars_,
                                   max_allowed_size_all_debug_urls_chars_,
                                   total_debug_urls_chars, log_context_);
    bid.set_interest_group_name(interest_group_name);
    *raw_response_.add_bids() = std::move(bid);
  }
  LogIfError(metric_context_->LogHistogram<metric::kBiddingZeroBidPercent>(
      (static_cast<double>(zero_bid_count)) / total_bid_count));
  PS_VLOG(kNoisyInfo, log_context_)
      << "\n\nFailed of total: " << failed_requests << "/" << output.size();
  benchmarking_logger_->HandleResponseEnd();
}

void GenerateBidsReactor::EncryptResponseAndFinish(grpc::Status status) {
  PS_VLOG(kPlain, log_context_)
      << "GenerateBidsRawResponse exported in EventMessage";
  log_context_.SetEventMessageField(raw_response_);
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(/*if_export_consented=*/true);

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
