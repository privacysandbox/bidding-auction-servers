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
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"
#include "src/cpp/util/status_macro/status_macros.h"
#include "src/cpp/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::google::protobuf::TextFormat;

using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using IGForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;
struct ParsedTrustedBiddingSignals {
  std::shared_ptr<std::string> json = std::make_shared<std::string>();
  absl::flat_hash_set<std::string> keys;
};
using TrustedBiddingSignalsByIg =
    absl::flat_hash_map<std::string,
                        absl::StatusOr<ParsedTrustedBiddingSignals>>;
constexpr int kArgsSizeWithWrapper = 6;

absl::StatusOr<std::string> ProtoToJson(
    const google::protobuf::Message& proto) {
  auto options = google::protobuf::util::JsonPrintOptions();
  std::string json;
  PS_RETURN_IF_ERROR(
      google::protobuf::util::MessageToJsonString(proto, &json, options));
  return json;
}

// Creates a json of trusted bidding signals for a single IG. Queries the
// bidding signals for -
// 1. IG Name and moves the bidding signal values to the new json document.
// 2. Bidding signal keys in the IG and copies them to the new json document.
absl::StatusOr<ParsedTrustedBiddingSignals> GetSignalsForIG(
    const GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding&
        ig,
    rapidjson::Value* bidding_signals_obj, long avg_signal_str_size) {
  // Insert bidding signal values for this Interest Group.
  ParsedTrustedBiddingSignals parsed_trusted_bidding_signals;
  rapidjson::Document ig_signals;
  ig_signals.SetObject();
  // If no bidding signals passed, return empty document.
  if (bidding_signals_obj == nullptr) {
    return parsed_trusted_bidding_signals;
  }

  // Copy bidding signals with key name in bidding signal keys.
  for (const auto& key : ig.trusted_bidding_signals_keys()) {
    if (parsed_trusted_bidding_signals.keys.contains(key)) {
      // Do not process duplicate keys.
      continue;
    }
    rapidjson::Value::ConstMemberIterator trusted_bidding_signals_key_itr =
        bidding_signals_obj->FindMember(key.c_str());
    if (trusted_bidding_signals_key_itr != bidding_signals_obj->MemberEnd()) {
      rapidjson::Value json_key;
      // Keep string reference. Assumes safe lifecycle.
      json_key.SetString(rapidjson::StringRef(key.c_str()));
      rapidjson::Value json_value;
      // Copy instead of move, could be referenced by multiple IGs.
      json_value.CopyFrom(trusted_bidding_signals_key_itr->value,
                          ig_signals.GetAllocator());
      // AddMember moves Values, do not reference them anymore.
      ig_signals.AddMember(json_key, json_value, ig_signals.GetAllocator());
      parsed_trusted_bidding_signals.keys.emplace(key);
    }
  }
  if (ig_signals.MemberCount() > 0) {
    absl::StatusOr<std::shared_ptr<std::string>> ig_signals_str =
        SerializeJsonDoc(ig_signals, avg_signal_str_size);
    PS_ASSIGN_OR_RETURN(parsed_trusted_bidding_signals.json, ig_signals_str);
  }
  return parsed_trusted_bidding_signals;
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
  absl::StrAppend(
      &device_signals_str, kJsonStringEnd, kJoinCount, kJsonValueStart,
      browser_signals.join_count(), kJsonValueEnd, kBidCount, kJsonValueStart,
      browser_signals.bid_count(), kJsonValueEnd, kRecency, kJsonValueStart,
      browser_signals.recency(), kJsonValueEnd, kPrevWins, kJsonValueStart,
      browser_signals.prev_wins().empty() ? kJsonEmptyString
                                          : browser_signals.prev_wins(),
      "}");
  return device_signals_str;
}

absl::StatusOr<std::string> SerializeRepeatedStringField(
    google::protobuf::RepeatedPtrField<std::string> repeated_string_field) {
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
absl::StatusOr<std::string> SerializeIG(IGForBidding ig) {
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

// Creates a map of Interest Group names -> trusted bidding signals json
// strings. Parses the trusted bidding signals string and calls GetSignalsForIG
// in a loop for all Interest Groups.
absl::StatusOr<TrustedBiddingSignalsByIg> SerializeTrustedBiddingSignalsPerIG(
    const GenerateBidsRequest::GenerateBidsRawRequest& raw_request,
    server_common::log::ContextImpl& log_context) {
  // Parse into JSON.
  auto start_parse_time = absl::Now();
  PS_ASSIGN_OR_RETURN((rapidjson::Document parsed_signals),
                      ParseJsonString(raw_request.bidding_signals()));
  PS_VLOG(2, log_context) << "\nTrusted Bidding Signals Deserialize Time: "
                          << ToInt64Microseconds(
                                 (absl::Now() - start_parse_time))
                          << " microseconds for "
                          << raw_request.bidding_signals().size() << " bytes.";

  // Select root key.
  if (!parsed_signals.HasMember("keys")) {
    PS_VLOG(2, log_context)
        << "Trusted bidding signals JSON validate error (Missing property "
           "\"keys\")";

    return absl::InvalidArgumentError(
        "Malformed trusted bidding signals (Missing property \"keys\")");
  }
  rapidjson::Value& bidding_signals_obj = parsed_signals["keys"];

  // Create IG -> TrustedBiddingSignals Map.
  TrustedBiddingSignalsByIg per_ig_signals_map;
  long avg_signal_size_per_ig = raw_request.bidding_signals().size() /
                                raw_request.interest_group_for_bidding_size();
  for (const auto& ig : raw_request.interest_group_for_bidding()) {
    per_ig_signals_map.try_emplace(
        ig.name(),
        GetSignalsForIG(ig, &bidding_signals_obj, avg_signal_size_per_ig));
  }
  return per_ig_signals_map;
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
    const TrustedBiddingSignalsByIg& ig_trusted_signals_map,
    const bool enable_buyer_debug_url_generation,
    server_common::log::ContextImpl& log_context,
    const bool enable_adtech_code_logging, const std::string& version) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest generate_bid_request;
  generate_bid_request.id = interest_group.name();
  // TODO(b/258790164) Update after code is fetched periodically.
  generate_bid_request.version_string = version;
  // Copy base input and amend with custom interest_group
  generate_bid_request.input = base_input;

  // IG must have trusted bidding signals to participate in Bidding.
  const auto& trusted_bidding_signals_itr =
      ig_trusted_signals_map.find(interest_group.name());
  if (trusted_bidding_signals_itr == ig_trusted_signals_map.end()) {
    return absl::InvalidArgumentError(
        "Interest Group must contain non-empty trusted bidding "
        "signals to generate bids.");
  }

  if (!trusted_bidding_signals_itr->second.ok()) {
    return trusted_bidding_signals_itr->second.status();
  }

  if (trusted_bidding_signals_itr->second.value().json->empty()) {
    return absl::InvalidArgumentError(
        "Interest Group must contain non-empty trusted bidding "
        "signals to generate bids.");
  }

  generate_bid_request
      .input[ArgIndex(GenerateBidArgs::kTrustedBiddingSignals)] =
      trusted_bidding_signals_itr->second.value().json;

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
                                 interest_group.browser_signals())) {
    PS_ASSIGN_OR_RETURN(std::string serialized_android_signals,
                        ProtoToJson(interest_group.android_signals()));
    generate_bid_request.input[ArgIndex(GenerateBidArgs::kDeviceSignals)] =
        std::make_shared<std::string>((serialized_android_signals.empty())
                                          ? R"JSON("")JSON"
                                          : serialized_android_signals);
  } else {
    generate_bid_request.input[ArgIndex(GenerateBidArgs::kDeviceSignals)] =
        std::make_shared<std::string>(R"JSON("")JSON");
  }
  generate_bid_request.input[ArgIndex(GenerateBidArgs::kFeatureFlags)] =
      std::make_shared<std::string>(
          GetFeatureFlagJson(enable_adtech_code_logging,
                             enable_buyer_debug_url_generation &&
                                 raw_request.enable_debug_reporting()));
  generate_bid_request.handler_name =
      kDispatchHandlerFunctionNameWithCodeWrapper;

  // Only add parsed keys.
  interest_group.clear_trusted_bidding_signals_keys();
  interest_group.mutable_trusted_bidding_signals_keys()->Add(
      trusted_bidding_signals_itr->second.value().keys.begin(),
      trusted_bidding_signals_itr->second.value().keys.end());
  auto start_parse_time = absl::Now();
  auto serialized_ig = SerializeIG(interest_group);
  if (!serialized_ig.ok()) {
    return serialized_ig.status();
  }
  generate_bid_request.input[ArgIndex(GenerateBidArgs::kInterestGroup)] =
      std::make_shared<std::string>(std::move(serialized_ig.value()));
  PS_VLOG(3, log_context)
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

// Set debug url if within character limit, otherwise clear field parsed from
// JSON response.
long SetAndReturnDebugUrlSize(AdWithBid* ad_with_bid,
                              int max_allowed_size_debug_url_chars,
                              long max_allowed_size_all_debug_urls_chars,
                              long current_all_debug_urls_chars) {
  long current_debug_urls_len = 0;
  if (!ad_with_bid->has_debug_report_urls()) {
    return current_debug_urls_len;
  }

  int win_url_length =
      ad_with_bid->debug_report_urls().auction_debug_win_url().length();
  if (win_url_length > max_allowed_size_debug_url_chars ||
      win_url_length + current_all_debug_urls_chars >
          max_allowed_size_all_debug_urls_chars) {
    ad_with_bid->mutable_debug_report_urls()->clear_auction_debug_win_url();
  } else {
    current_all_debug_urls_chars += win_url_length;
    current_debug_urls_len += win_url_length;
  }

  int loss_url_length =
      ad_with_bid->debug_report_urls().auction_debug_loss_url().length();
  if (loss_url_length > max_allowed_size_debug_url_chars ||
      loss_url_length + current_all_debug_urls_chars >
          max_allowed_size_all_debug_urls_chars) {
    ad_with_bid->mutable_debug_report_urls()->clear_auction_debug_loss_url();
  } else {
    current_debug_urls_len += loss_url_length;
  }
  return current_debug_urls_len;
}
}  // namespace

GenerateBidsReactor::GenerateBidsReactor(
    CodeDispatchClient& dispatcher, const GenerateBidsRequest* request,
    GenerateBidsResponse* response,
    std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BiddingServiceRuntimeConfig& runtime_config)
    : BaseGenerateBidsReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse>(
          dispatcher, runtime_config, request, response, key_fetcher_manager,
          crypto_client),
      benchmarking_logger_(std::move(benchmarking_logger)),
      auction_scope_(raw_request_.top_level_seller().empty()
                         ? AuctionScope::kSingleSeller
                         : AuctionScope::kDeviceComponentSeller),
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
  benchmarking_logger_->BuildInputBegin();

  PS_VLOG(kEncrypted, log_context_) << "Encrypted GenerateBidsRequest:\n"
                                    << request_->ShortDebugString();
  PS_VLOG(kPlain, log_context_) << "GenerateBidsRawRequest:\n"
                                << raw_request_.ShortDebugString();

  auto interest_groups = raw_request_.interest_group_for_bidding();

  // Parse trusted bidding signals
  absl::StatusOr<TrustedBiddingSignalsByIg> ig_trusted_signals_map =
      SerializeTrustedBiddingSignalsPerIG(raw_request_, log_context_);
  if (!ig_trusted_signals_map.ok()) {
    PS_VLOG(0, log_context_) << "Request failed while parsing bidding signals: "
                             << ig_trusted_signals_map.status().ToString(
                                    absl::StatusToStringMode::kWithEverything);
    EncryptResponseAndFinish(
        server_common::FromAbslStatus(ig_trusted_signals_map.status()));
    return;
  }

  // Build base input.
  std::vector<std::shared_ptr<std::string>> base_input =
      BuildBaseInput(raw_request_);
  for (int i = 0; i < interest_groups.size(); i++) {
    absl::StatusOr<DispatchRequest> generate_bid_request =
        BuildGenerateBidRequest(interest_groups.at(i), raw_request_, base_input,
                                ig_trusted_signals_map.value(),
                                enable_buyer_debug_url_generation_,
                                log_context_, enable_adtech_code_logging_,
                                protected_auction_generate_bid_version_);
    if (!generate_bid_request.ok()) {
      PS_VLOG(3, log_context_)
          << "Unable to build GenerateBidRequest: "
          << generate_bid_request.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    } else {
      auto dispatch_request = generate_bid_request.value();
      dispatch_request.tags[kTimeoutMs] = roma_timeout_ms_;
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
      [this, start_js_execution_time](
          const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        int js_execution_time_ms =
            (absl::Now() - start_js_execution_time) / absl::Milliseconds(1);
        LogIfError(metric_context_->LogHistogram<metric::kJSExecutionDuration>(
            js_execution_time_ms));
        GenerateBidsCallback(result);
        EncryptResponseAndFinish(grpc::Status::OK);
      });

  if (!status.ok()) {
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                       1, metric::kBiddingGenerateBidsFailedToDispatchCode));
    PS_VLOG(1, log_context_)
        << "Execution request failed for batch: " << raw_request_.DebugString()
        << status.ToString(absl::StatusToStringMode::kWithEverything);
    LogIfError(
        metric_context_->LogUpDownCounter<metric::kJSExecutionErrorCount>(1));
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
      PS_VLOG(2, log_context_)
          << "Generate Bids V8 Response: " << dispatch_response.status();
      if (dispatch_response.ok()) {
        PS_VLOG(2, log_context_) << dispatch_response.value().resp;
      }
    }
  }
  benchmarking_logger_->HandleResponseBegin();
  int total_bid_count = static_cast<int>(output.size());
  int zero_bid_count = 0;
  LogIfError(metric_context_->AccumulateMetric<metric::kBiddingTotalBidsCount>(
      total_bid_count));
  int failed_requests = 0;
  long current_all_debug_urls_chars = 0;
  for (int i = 0; i < output.size(); i++) {
    auto& result = output.at(i);
    bool is_bid_zero = true;
    if (result.ok()) {
      AdWithBid bid;
      absl::StatusOr<std::string> generate_bid_response =
          ParseAndGetResponseJson(enable_adtech_code_logging_, result->resp,
                                  log_context_);
      if (!generate_bid_response.ok()) {
        PS_VLOG(0, log_context_)
            << "Failed to parse response from Roma "
            << generate_bid_response.status().ToString(
                   absl::StatusToStringMode::kWithEverything);
      }
      auto valid = google::protobuf::util::JsonStringToMessage(
          generate_bid_response.value(), &bid);
      const std::string interest_group_name = result->id;
      if (valid.ok()) {
        if (current_all_debug_urls_chars >=
            max_allowed_size_all_debug_urls_chars_) {
          bid.clear_debug_report_urls();
        } else {
          current_all_debug_urls_chars +=
              SetAndReturnDebugUrlSize(&bid, max_allowed_size_debug_url_chars_,
                                       max_allowed_size_all_debug_urls_chars_,
                                       current_all_debug_urls_chars);
        }
        if (!IsValidBid(bid)) {
          PS_VLOG(2, log_context_)
              << "Skipping 0 bid for " << interest_group_name << ": "
              << bid.DebugString();
        } else if (
            // If this is a component auction and bid is not allowed, skip it.
            auction_scope_ == AuctionScope::kDeviceComponentSeller &&
            !bid.allow_component_auction()) {
          // TODO(b/311234165): Add metric for rejected component ads.
          PS_VLOG(1, log_context_)
              << "Skipping component bid as it is not allowed for "
              << interest_group_name << ": " << bid.DebugString();
        } else {
          bid.set_interest_group_name(interest_group_name);
          *raw_response_.add_bids() = bid;
          is_bid_zero = false;
        }
      } else {
        PS_VLOG(1, log_context_)
            << "Invalid json output from code execution for interest_group "
            << interest_group_name << ": " << result->resp;
      }
    } else {
      failed_requests = failed_requests + 1;
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                         1, metric::kBiddingGenerateBidsDispatchResponseError));
      PS_VLOG(1, log_context_)
          << "Invalid execution (possibly invalid input): "
          << result.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    }
    if (is_bid_zero) {
      zero_bid_count += 1;
      LogIfError(
          metric_context_->AccumulateMetric<metric::kBiddingZeroBidCount>(1));
    }
  }
  LogIfError(metric_context_->LogHistogram<metric::kBiddingZeroBidPercent>(
      (static_cast<double>(zero_bid_count)) / total_bid_count));

  PS_VLOG(1, log_context_) << "\n\nFailed of total: " << failed_requests << "/"
                           << output.size();
  benchmarking_logger_->HandleResponseEnd();
}

void GenerateBidsReactor::EncryptResponseAndFinish(grpc::Status status) {
  PS_VLOG(kPlain, log_context_) << "GenerateBidsRawResponse\n"
                                << raw_response_.DebugString();

  if (!EncryptResponse()) {
    PS_VLOG(1, log_context_) << "Failed to encrypt the generate bids response.";
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
