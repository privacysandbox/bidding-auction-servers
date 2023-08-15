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
#include "glog/logging.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/status_macros.h"
#include "services/common/util/status_util.h"

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

constexpr char kDispatchHandlerFunctionName[] = "generateBid";

constexpr char kDispatchHandlerWrapperFunctionName[] = "generateBidWrapper";
constexpr char kDispatchHandlerFunctionNameWithCodeWrapper[] =
    "generateBidEntryFunction";
constexpr char kRomaTimeoutMs[] = "TimeoutMs";

constexpr int kArgsSizeDefault = 5;
constexpr int kArgsSizeWithWrapper = 6;
constexpr char kDisableAdTechCodeLogging[] = "false";
constexpr char kEnableAdTechCodeLogging[] = "true";

std::string ProtoToJson(const google::protobuf::Message& proto) {
  auto options = google::protobuf::util::JsonPrintOptions();
  std::string json;
  google::protobuf::util::MessageToJsonString(proto, &json, options);
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
  // Move bidding signals with key name = IG name.
  rapidjson::Value::MemberIterator ig_name_itr =
      bidding_signals_obj->FindMember(ig.name().c_str());

  if (ig_name_itr != bidding_signals_obj->MemberEnd()) {
    rapidjson::Value json_key;
    // Keep string reference. Assumes safe lifecycle.
    json_key.SetString(rapidjson::StringRef(ig.name().c_str()));
    // AddMember moves Values, do not reference them anymore.
    ig_signals.AddMember(json_key, ig_name_itr->value,
                         ig_signals.GetAllocator());
    parsed_trusted_bidding_signals.keys.emplace(ig.name());
  }

  // Copy bidding signals with key name in bidding signal keys.
  for (const auto& key : ig.trusted_bidding_signals_keys()) {
    if (parsed_trusted_bidding_signals.keys.contains(key)) {
      // Do not process duplicate keys.
      continue;
    }
    rapidjson::Value::ConstMemberIterator key_itr =
        bidding_signals_obj->FindMember(key.c_str());
    if (key_itr != bidding_signals_obj->MemberEnd()) {
      rapidjson::Value json_key;
      // Keep string reference. Assumes safe lifecycle.
      json_key.SetString(rapidjson::StringRef(key.c_str()));
      rapidjson::Value json_value;
      // Copy instead of move, could be referenced by multiple IGs.
      json_value.CopyFrom(key_itr->value, ig_signals.GetAllocator());
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

std::string MakeBrowserSignalsForScript(absl::string_view publisher_name,
                                        absl::string_view seller,
                                        BrowserSignals browser_signals) {
  return absl::StrCat(
      R"JSON({")JSON", kTopWindowHostname, R"JSON(":")JSON", publisher_name,
      R"JSON(",")JSON", kSeller, R"JSON(":")JSON", seller, R"JSON(",")JSON",
      kTopLevelSeller, R"JSON(":")JSON", seller, R"JSON(",")JSON", kJoinCount,
      R"JSON(":)JSON", browser_signals.join_count(), R"JSON(,")JSON", kBidCount,
      R"JSON(":)JSON", browser_signals.bid_count(), R"JSON(,")JSON", kRecency,
      R"JSON(":)JSON", browser_signals.recency(), R"JSON(,")JSON", kPrevWins,
      R"JSON(":)JSON",
      browser_signals.prev_wins().empty() ? R"JSON("")JSON"
                                          : browser_signals.prev_wins(),
      "}");
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
    const ContextLogger& logger) {
  // Parse into JSON.
  auto start_parse_time = absl::Now();
  PS_ASSIGN_OR_RETURN((rapidjson::Document parsed_signals),
                      ParseJsonString(raw_request.bidding_signals()));
  logger.vlog(2, "\nTrusted Bidding Signals Deserialize Time: ",
              ToInt64Microseconds((absl::Now() - start_parse_time)),
              " microseconds for ", raw_request.bidding_signals().size(),
              " bytes.");

  // Select root key.
  if (!parsed_signals.HasMember("keys")) {
    logger.vlog(2,
                "Trusted bidding signals JSON validate error (Missing property "
                "\"keys\")");
    return absl::InvalidArgumentError(
        "Malformatted trusted bidding signals (Missing property \"keys\")");
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

// See GenerateBidInput for more detail on each field.
enum class GenerateBidArgs : int {
  kInterestGroup = 0,
  kAuctionSignals,
  kBuyerSignals,
  kTrustedBiddingSignals,
  kDeviceSignals,
  kFeatureFlags
};

constexpr int BidArgIndex(GenerateBidArgs arg) {
  return static_cast<std::underlying_type_t<GenerateBidArgs>>(arg);
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
  input[BidArgIndex(GenerateBidArgs::kAuctionSignals)] =
      std::make_shared<std::string>((raw_request.auction_signals().empty())
                                        ? "\"\""
                                        : raw_request.auction_signals());
  input[BidArgIndex(GenerateBidArgs::kBuyerSignals)] =
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
    const bool enable_buyer_debug_url_generation, const ContextLogger& logger,
    const bool enable_adtech_code_logging) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest generate_bid_request;
  generate_bid_request.id = interest_group.name();
  // TODO(b/258790164) Update after code is fetched periodically.
  generate_bid_request.version_num = 1;
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
      .input[BidArgIndex(GenerateBidArgs::kTrustedBiddingSignals)] =
      trusted_bidding_signals_itr->second.value().json;

  // IG must have device signals to participate in Bidding.
  google::protobuf::util::MessageDifferencer differencer;
  if (interest_group.has_browser_signals() &&
      interest_group.browser_signals().IsInitialized() &&
      !differencer.Equals(BrowserSignals::default_instance(),
                          interest_group.browser_signals())) {
    generate_bid_request.input[BidArgIndex(GenerateBidArgs::kDeviceSignals)] =
        std::make_shared<std::string>(MakeBrowserSignalsForScript(
            raw_request.publisher_name(), raw_request.seller(),
            interest_group.browser_signals()));
  } else if (interest_group.has_android_signals() &&
             interest_group.android_signals().IsInitialized() &&
             !differencer.Equals(AndroidSignals::default_instance(),
                                 interest_group.browser_signals())) {
    std::string serialized_android_signals =
        ProtoToJson(interest_group.android_signals());
    generate_bid_request.input[BidArgIndex(GenerateBidArgs::kDeviceSignals)] =
        std::make_shared<std::string>((serialized_android_signals.empty())
                                          ? R"JSON("")JSON"
                                          : serialized_android_signals);
  } else {
    generate_bid_request.input[BidArgIndex(GenerateBidArgs::kDeviceSignals)] =
        std::make_shared<std::string>(R"JSON("")JSON");
  }
  generate_bid_request.input[BidArgIndex(GenerateBidArgs::kFeatureFlags)] =
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
  generate_bid_request.input[BidArgIndex(GenerateBidArgs::kInterestGroup)] =
      std::make_shared<std::string>(std::move(serialized_ig.value()));
  logger.vlog(
      3, "\nInterest Group Serialize Time: ",
      ToInt64Microseconds((absl::Now() - start_parse_time)),
      " microseconds for ",
      generate_bid_request.input[BidArgIndex(GenerateBidArgs::kInterestGroup)]
          ->size(),
      " bytes.");
  if (VLOG_IS_ON(2)) {
    logger.vlog(2, "\n\nGenerateBid Input Args:");
    for (const auto& it : generate_bid_request.input) {
      logger.vlog(2, it->c_str(), "\n");
    }
  }
  return generate_bid_request;
}

absl::StatusOr<std::string> ParseAndGetGenerateBidResponseJson(
    bool enable_adtech_code_logging, const std::string& response,
    const ContextLogger& logger) {
  PS_ASSIGN_OR_RETURN(rapidjson::Document document, ParseJsonString(response));
  rapidjson::Value& response_obj = document["response"];
  std::string response_json;
  PS_ASSIGN_OR_RETURN(response_json, SerializeJsonDoc(response_obj));
  if (enable_adtech_code_logging) {
    const rapidjson::Value& logs = document["logs"];
    for (const auto& log : logs.GetArray()) {
      logger.vlog(1, "Logs: ", log.GetString());
    }
    const rapidjson::Value& warnings = document["warnings"];
    for (const auto& warning : warnings.GetArray()) {
      logger.vlog(1, "Warnings: ", warning.GetString());
    }
    const rapidjson::Value& errors = document["errors"];
    for (const auto& error : errors.GetArray()) {
      logger.vlog(1, "Errors: ", error.GetString());
    }
  }
  return response_json;
}

}  // namespace

GenerateBidsReactor::GenerateBidsReactor(
    const CodeDispatchClient& dispatcher, const GenerateBidsRequest* request,
    GenerateBidsResponse* response,
    std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BiddingServiceRuntimeConfig& runtime_config)
    : CodeDispatchReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse>(
          dispatcher, request, response, key_fetcher_manager, crypto_client,
          runtime_config.encryption_enabled),
      benchmarking_logger_(std::move(benchmarking_logger)),
      enable_buyer_debug_url_generation_(
          runtime_config.enable_buyer_debug_url_generation),
      enable_adtech_code_logging_(runtime_config.enable_adtech_code_logging),
      roma_timeout_ms_(runtime_config.roma_timeout_ms) {
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::BiddingContextMap()->Remove(request_));
    return absl::OkStatus();
  }()) << "BiddingContextMap()->Get(request) should have been called";
}

void GenerateBidsReactor::Execute() {
  benchmarking_logger_->BuildInputBegin();
  logger_ = ContextLogger(GetLoggingContext(raw_request_));
  auto interest_groups = raw_request_.interest_group_for_bidding();

  // Parse trusted bidding signals
  absl::StatusOr<TrustedBiddingSignalsByIg> ig_trusted_signals_map =
      SerializeTrustedBiddingSignalsPerIG(raw_request_, logger_);
  if (!ig_trusted_signals_map.ok()) {
    logger_.vlog(0, "Request failed while parsing bidding signals: ",
                 ig_trusted_signals_map.status().ToString(
                     absl::StatusToStringMode::kWithEverything));
    EncryptResponseAndFinish(FromAbslStatus(ig_trusted_signals_map.status()));
    return;
  }

  // Build base input.
  std::vector<std::shared_ptr<std::string>> base_input =
      BuildBaseInput(raw_request_);
  for (int i = 0; i < interest_groups.size(); i++) {
    absl::StatusOr<DispatchRequest> generate_bid_request =
        BuildGenerateBidRequest(interest_groups.at(i), raw_request_, base_input,
                                ig_trusted_signals_map.value(),
                                enable_buyer_debug_url_generation_, logger_,
                                enable_adtech_code_logging_);
    if (!generate_bid_request.ok()) {
      if (VLOG_IS_ON(3)) {
        logger_.vlog(3, "Unable to build GenerateBidRequest: ",
                     generate_bid_request.status().ToString(
                         absl::StatusToStringMode::kWithEverything));
      }
    } else {
      auto dispatch_request = generate_bid_request.value();
      dispatch_request.tags[kRomaTimeoutMs] = roma_timeout_ms_;
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
    std::string original_request;
    TextFormat::PrintToString(raw_request_, &original_request);
    logger_.vlog(1, "Execution request failed for batch: ", original_request,
                 status.ToString(absl::StatusToStringMode::kWithEverything));
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
  if (VLOG_IS_ON(2)) {
    for (const auto& dispatch_response : output) {
      logger_.vlog(2,
                   "Generate Bids V8 Response: ", dispatch_response.status());
      if (dispatch_response.ok()) {
        logger_.vlog(2, dispatch_response.value().resp);
      }
    }
  }
  benchmarking_logger_->HandleResponseBegin();
  int failed_requests = 0;
  for (int i = 0; i < output.size(); i++) {
    auto& result = output.at(i);
    if (result.ok()) {
      AdWithBid bid;
      absl::StatusOr<std::string> generate_bid_response =
          ParseAndGetGenerateBidResponseJson(enable_adtech_code_logging_,
                                             result->resp, logger_);
      if (!generate_bid_response.ok()) {
        logger_.vlog(0, "Failed to parse response from Roma ",
                     generate_bid_response.status().ToString(
                         absl::StatusToStringMode::kWithEverything));
      }
      auto valid = google::protobuf::util::JsonStringToMessage(
          generate_bid_response.value(), &bid);
      const std::string interest_group_name = result->id;
      if (valid.ok()) {
        if (bid.bid() == 0.0f && !bid.has_debug_report_urls()) {
          logger_.vlog(2, "Skipping 0 bid for ", interest_group_name, ": ",
                       bid.DebugString());
        } else {
          bid.set_interest_group_name(interest_group_name);
          *raw_response_.add_bids() = bid;
        }
      } else {
        logger_.vlog(
            1, "Invalid json output from code execution for interest_group ",
            interest_group_name, ": ", result->resp);
      }
    } else {
      failed_requests = failed_requests + 1;
      logger_.vlog(
          1, "Invalid execution (possibly invalid input): ",
          result.status().ToString(absl::StatusToStringMode::kWithEverything));
    }
  }

  logger_.vlog(1, "\n\nFailed of total: ", failed_requests, "/", output.size());
  benchmarking_logger_->HandleResponseEnd();
  logger_.vlog(2, "GenerateBidsResponse:\n", raw_response_.DebugString());
}

void GenerateBidsReactor::EncryptResponseAndFinish(grpc::Status status) {
  DCHECK(encryption_enabled_);
  if (!EncryptResponse()) {
    logger_.vlog(1, "Failed to encrypt the generate bids response.");
    status = grpc::Status(grpc::INTERNAL, kInternalServerError);
  }
  if (status.error_code() == grpc::StatusCode::OK) {
    metric_context_->SetRequestSuccessful();
  }
  Finish(status);
}

ContextLogger::ContextMap GenerateBidsReactor::GetLoggingContext(
    const GenerateBidsRequest::GenerateBidsRawRequest& generate_bids_request) {
  const auto& logging_context = generate_bids_request.log_context();
  return {{kGenerationId, logging_context.generation_id()},
          {kAdtechDebugId, logging_context.adtech_debug_id()}};
}

void GenerateBidsReactor::OnDone() { delete this; }

}  // namespace privacy_sandbox::bidding_auction_servers
