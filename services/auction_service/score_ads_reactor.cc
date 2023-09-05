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

#include "score_ads_reactor.h"

#include <algorithm>
#include <limits>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_replace.h"
#include "glog/log_severity.h"
#include "glog/logging.h"
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/pointer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/util/json_util.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/status_macros.h"
#include "services/common/util/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;

constexpr char DispatchHandlerFunctionWithSellerWrapper[] =
    "scoreAdEntryFunction";
constexpr char kAdComponentRenderUrlsProperty[] = "adComponentRenderUrls";
constexpr char kRenderUrlsPropertyForKVResponse[] = "renderUrls";
constexpr char kRenderUrlsPropertyForScoreAd[] = "renderUrl";
constexpr char kRomaTimeoutMs[] = "TimeoutMs";
constexpr int kArgSizeDefault = 6;
constexpr int kArgSizeWithWrapper = 7;

// The following fields are expected to returned by ScoreAd response
constexpr char kDesirabilityPropertyForScoreAd[] = "desirability";
constexpr char kAllowComponentAuctionPropertyForScoreAd[] =
    "allowComponentAuction";
constexpr char kRejectReasonPropertyForScoreAd[] = "rejectReason";
constexpr char kDebugReportUrlsPropertyForScoreAd[] = "debugReportUrls";
constexpr char kAuctionDebugLossUrlPropertyForScoreAd[] = "auctionDebugLossUrl";
constexpr char kAuctionDebugWinUrlPropertyForScoreAd[] = "auctionDebugWinUrl";

// TODO(b/259610873): Revert hardcoded device signals.
std::string MakeDeviceSignals(
    absl::string_view publisher_hostname,
    absl::string_view interest_group_owner, absl::string_view render_url,
    const google::protobuf::RepeatedPtrField<std::string>&
        ad_component_render_urls) {
  std::string device_signals =
      absl::StrCat("{", R"("interestGroupOwner":")", interest_group_owner, "\"",
                   R"(,"topWindowHostname":")", publisher_hostname, "\"");

  if (!ad_component_render_urls.empty()) {
    absl::StrAppend(&device_signals, R"(,"adComponents":[)");
    for (int i = 0; i < ad_component_render_urls.size(); i++) {
      absl::StrAppend(&device_signals, "\"", ad_component_render_urls.at(i),
                      "\"");
      if (i != ad_component_render_urls.size() - 1) {
        absl::StrAppend(&device_signals, ",");
      }
    }
    absl::StrAppend(&device_signals, R"(])");
  }
  absl::StrAppend(&device_signals, ",\"", kRenderUrlsPropertyForScoreAd,
                  "\":\"", render_url, "\"}");
  return device_signals;
}

// See ScoreAdInput for more detail on each field.
enum class ScoreAdArgs : int {
  kAdMetadata = 0,
  kBid,
  kAuctionConfig,
  kScoringSignals,
  kDeviceSignals,
  // This is only added to prevent errors in the score ad script, and
  // will always be an empty object.
  kDirectFromSellerSignals,
  kFeatureFlags,
};

constexpr int ScoreArgIndex(ScoreAdArgs arg) {
  return static_cast<std::underlying_type_t<ScoreAdArgs>>(arg);
}

/**
 * Builds the ScoreAdInput, following the description here:
 * https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_api.md#scoreads
 * and here
 * https://github.com/WICG/turtledove/blob/main/FLEDGE.md#23-scoring-bids.
 *
 * NOTE: All inputs MUST be valid JSON, not just something Javascript would
 * accept. Property names need to be in quotes! Additionally: See issues with
 * input formatting in b/258697130.
 */
std::vector<std::shared_ptr<std::string>> ScoreAdInput(
    const AdWithBidMetadata& ad, std::shared_ptr<std::string> auction_config,
    const absl::string_view publisher_hostname,
    const absl::flat_hash_map<std::string, rapidjson::StringBuffer>&
        scoring_signals,
    const ContextLogger& logger, bool enable_adtech_code_logging,
    bool enable_debug_reporting) {
  std::vector<std::shared_ptr<std::string>> input(
      kArgSizeWithWrapper);  // ScoreAdArgs size

  // TODO: b/260265272
  std::string adMetadataAsJson;
  const auto& it = ad.ad().struct_value().fields().find("metadata");
  if (it != ad.ad().struct_value().fields().end()) {
    google::protobuf::util::MessageToJsonString(it->second, &adMetadataAsJson);
  }
  input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)] =
      std::make_shared<std::string>(adMetadataAsJson);
  input[ScoreArgIndex(ScoreAdArgs::kBid)] =
      std::make_shared<std::string>(std::to_string(ad.bid()));
  input[ScoreArgIndex(ScoreAdArgs::kAuctionConfig)] = auction_config;
  // TODO(b/258697130): Roma client string support bug
  input[ScoreArgIndex(ScoreAdArgs::kScoringSignals)] =
      std::make_shared<std::string>(
          scoring_signals.at(ad.render()).GetString());
  input[ScoreArgIndex(ScoreAdArgs::kDeviceSignals)] =
      std::make_shared<std::string>(
          MakeDeviceSignals(publisher_hostname, ad.interest_group_owner(),
                            ad.render(), ad.ad_components()));
  // This is only added to prevent errors in the score ad script, and
  // will always be an empty object.
  input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)] =
      std::make_shared<std::string>("{}");
  input[ScoreArgIndex(ScoreAdArgs::kFeatureFlags)] =
      std::make_shared<std::string>(GetFeatureFlagJson(
          enable_adtech_code_logging, enable_debug_reporting));

  if (VLOG_IS_ON(2)) {
    logger.vlog(2, "\n\nScore Ad Input Args:", "\nAdMetadata:\n",
                *(input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)]), "\nBid:\n",
                *(input[ScoreArgIndex(ScoreAdArgs::kBid)]),
                "\nAuction Config:\n",
                *(input[ScoreArgIndex(ScoreAdArgs::kAuctionConfig)]),
                "\nScoring Signals:\n",
                *(input[ScoreArgIndex(ScoreAdArgs::kScoringSignals)]),
                "\nDevice Signals:\n",
                *(input[ScoreArgIndex(ScoreAdArgs::kDeviceSignals)]),
                "\nDirectFromSellerSignals:\n",
                (input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)]));
  }
  return input;
}

DispatchRequest BuildScoreAdRequest(
    const AdWithBidMetadata& ad, std::shared_ptr<std::string> auction_config,
    const absl::string_view publisher_hostname,
    const absl::flat_hash_map<std::string, rapidjson::StringBuffer>&
        scoring_signals,
    const bool enable_debug_reporting, const ContextLogger& logger,
    const bool enable_adtech_code_logging) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest score_ad_request;
  // TODO(b/250893468) Revisit dispatch id.
  score_ad_request.id = ad.render();
  // TODO(b/258790164) Update after code is fetched periodically.
  score_ad_request.version_num = 1;
  score_ad_request.handler_name = DispatchHandlerFunctionWithSellerWrapper;

  score_ad_request.input =
      ScoreAdInput(ad, auction_config, publisher_hostname, scoring_signals,
                   logger, enable_adtech_code_logging, enable_debug_reporting);
  return score_ad_request;
}

// Builds a map of render urls to JSON objects holding the scoring signals.
// An entry looks like: url_to_signals["fooAds.com/123"] = {"fooAds.com/123":
// {"some", "scoring", "signals"}}. Notice the render URL is present in the map
// both as its key, and again in each entry as a key to the JSON object. This is
// intentional, as it allows moving the key to the new signals objects being
// built for each AdWithBid.
absl::flat_hash_map<std::string, rapidjson::Document> BuildAdScoringSignalsMap(
    rapidjson::Value& trusted_scoring_signals_value) {
  absl::flat_hash_map<std::string, rapidjson::Document> url_to_signals;
  for (rapidjson::Value::MemberIterator itr =
           trusted_scoring_signals_value.MemberBegin();
       itr != trusted_scoring_signals_value.MemberEnd(); ++itr) {
    // Moving the name will render it inaccessible unless a copy is made first.
    std::string ad_url = itr->name.GetString();
    // A Document rather than a Value is created, as a Document has an
    // Allocator.
    rapidjson::Document ad_details;
    ad_details.SetObject();
    // AddMember moves itr's Values, do not reference them anymore.
    ad_details.AddMember(itr->name, itr->value, ad_details.GetAllocator());
    url_to_signals.try_emplace(std::move(ad_url), std::move(ad_details));
  }
  return url_to_signals;
}

// Builds a set of ad component render urls for components used in more than one
// ad with bid. Since a single ad component might be a part of multiple ads, and
// since rapidjson enforces moves over copies unless explicitly specified
// otherwise (that's what makes it "rapid"), we need to know which ad component
// scoring signals need to be copied rather than moved. Otherwise, for an ad
// component used n times, moving its signals would fail on all times except the
// first. O(n) operation, where n is the number of ad component render urls.
absl::flat_hash_set<std::string> FindSharedComponentUrls(
    const google::protobuf::RepeatedPtrField<AdWithBidMetadata>&
        ads_with_bids) {
  absl::flat_hash_map<std::string, int> url_occurrences;
  absl::flat_hash_set<std::string> multiple_occurrence_component_urls;
  for (const auto& ad_with_bid : ads_with_bids) {
    for (const auto& ad_component_render_url : ad_with_bid.ad_components()) {
      ++url_occurrences[ad_component_render_url];
      if (url_occurrences[ad_component_render_url] > 1) {
        std::string copy_of_ad_component_render_url = ad_component_render_url;
        multiple_occurrence_component_urls.emplace(
            std::move(copy_of_ad_component_render_url));
      }
    }
  }
  return multiple_occurrence_component_urls;
}

// Adds scoring signals for ad component render urls to signals object
// for a single ad with bid, `ad_with_bid`.
// `ad_with_bid` and `multiple_occurrence_component_urls` will only
// be read from. `component_signals` must be filled with the trusted scoring
// signals for this ad's ad component render urls. If an ad component render url
// is not in `multiple_occurrence_component_urls`, then its signals will be
// moved out from `component signals`. Else they will be copied for use here and
// left intact for use in future calls to this method.
// Returns a JSON document that contains the scoring signals for all ad
// component render URLs in this ad_with_bid.
absl::StatusOr<rapidjson::Document> AddComponentSignals(
    const AdWithBidMetadata& ad_with_bid,
    const absl::flat_hash_set<std::string>& multiple_occurrence_component_urls,
    absl::flat_hash_map<std::string, rapidjson::Document>& component_signals) {
  // Create overall signals object.
  rapidjson::Document combined_signals_for_this_bid;
  combined_signals_for_this_bid.SetObject();
  // Create empty expandable object to add ad component signals to.
  rapidjson::Document empty_object;
  empty_object.SetObject();
  // Add the expandable object to the combined signals object.
  auto combined_signals_itr =
      combined_signals_for_this_bid
          .AddMember(kAdComponentRenderUrlsProperty, empty_object,
                     combined_signals_for_this_bid.GetAllocator())
          .MemberBegin();

  for (const auto& ad_component_render_url : ad_with_bid.ad_components()) {
    // Check if there are signals for this component ad.
    auto component_url_signals_itr =
        component_signals.find(ad_component_render_url);
    if (component_url_signals_itr == component_signals.end()) {
      continue;
    }
    // Copy only if necessary.
    rapidjson::Document to_add;
    if (multiple_occurrence_component_urls.contains(ad_component_render_url)) {
      // Use the allocator of the object to which these signals are
      // ultimately going.
      to_add.CopyFrom(component_url_signals_itr->second,
                      combined_signals_for_this_bid.GetAllocator());
    } else {
      to_add.Swap(component_url_signals_itr->second);
    }
    // Grab member to avoid finding it twice.
    auto comp_url_signals_to_move_from =
        to_add.FindMember(ad_component_render_url.c_str());
    // This should never happen, the map entry's signals should always
    // be for the URL on which the map is keyed.
    if (comp_url_signals_to_move_from == to_add.MemberEnd()) {
      // This can only be caused by an error forming the map on our
      // side.
      return absl::Status(
          absl::StatusCode::kInternal,
          "Internal error while processing trusted scoring signals.");
    }
    // AddMember moves the values input into it, do not reference them
    // anymore.
    combined_signals_itr->value.AddMember(
        comp_url_signals_to_move_from->name,
        comp_url_signals_to_move_from->value,
        combined_signals_for_this_bid.GetAllocator());
  }
  return combined_signals_for_this_bid;
}

absl::StatusOr<absl::flat_hash_map<std::string, rapidjson::StringBuffer>>
BuildTrustedScoringSignals(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
    const ContextLogger& logger) {
  rapidjson::Document trusted_scoring_signals_value;
  // TODO (b/285214424): De-nest, use a guard.
  if (!raw_request.scoring_signals().empty()) {
    // Attempt to parse into an object.
    auto start_parse_time = absl::Now();
    rapidjson::ParseResult parse_result =
        trusted_scoring_signals_value.Parse<rapidjson::kParseFullPrecisionFlag>(
            raw_request.scoring_signals().data());
    if (parse_result.IsError()) {
      // TODO (b/285215004): Print offset to ease debugging.
      logger.vlog(2, "Trusted scoring signals JSON parse error: ",
                  rapidjson::GetParseError_En(parse_result.Code()));
      return absl::InvalidArgumentError("Malformed trusted scoring signals");
    }
    // Build a map of the signals for each render URL.
    auto render_urls_itr = trusted_scoring_signals_value.FindMember(
        kRenderUrlsPropertyForKVResponse);
    if (render_urls_itr == trusted_scoring_signals_value.MemberEnd()) {
      // If there are no scoring signals for any render urls, none can be
      // scored. Abort now.
      return absl::InvalidArgumentError(
          "Trusted scoring signals include no render urls.");
    }
    absl::flat_hash_map<std::string, rapidjson::Document> render_url_signals =
        BuildAdScoringSignalsMap(render_urls_itr->value);
    // No scoring signals for ad component render urls are required,
    // however if present we build a map to their scoring signals in the same
    // way.
    absl::flat_hash_map<std::string, rapidjson::Document> component_signals;
    auto component_urls_itr = trusted_scoring_signals_value.FindMember(
        kAdComponentRenderUrlsProperty);
    if (component_urls_itr != trusted_scoring_signals_value.MemberEnd()) {
      component_signals = BuildAdScoringSignalsMap(component_urls_itr->value);
    }

    // Find the ad component render urls used more than once so we know which
    // signals we must copy rather than move.
    absl::flat_hash_set<std::string> multiple_occurrence_component_urls =
        FindSharedComponentUrls(raw_request.ad_bids());
    // Each AdWithBid needs signals for both its render URL and its ad component
    // render urls.
    absl::flat_hash_map<std::string, rapidjson::Document> combined_signals;
    for (const auto& ad_with_bid : raw_request.ad_bids()) {
      // Now that we have a map of all component signals and all ad signals, we
      // can build the object.
      // Check for the render URL's signals; skip if none.
      // (Ad with bid will not be scored anyways in that case.)
      auto render_url_signals_itr =
          render_url_signals.find(ad_with_bid.render());
      if (render_url_signals_itr == render_url_signals.end()) {
        continue;
      }
      absl::StatusOr<rapidjson::Document> combined_signals_for_this_bid;
      PS_ASSIGN_OR_RETURN(
          combined_signals_for_this_bid,
          AddComponentSignals(ad_with_bid, multiple_occurrence_component_urls,
                              component_signals));
      // Do not reference values after move.
      combined_signals_for_this_bid.value().AddMember(
          kRenderUrlsPropertyForScoreAd, render_url_signals_itr->second,
          combined_signals_for_this_bid.value().GetAllocator());
      combined_signals.try_emplace(
          ad_with_bid.render(),
          std::move(combined_signals_for_this_bid.value()));
    }

    // Now turn the editable JSON documents into string buffers before
    // returning.
    absl::flat_hash_map<std::string, rapidjson::StringBuffer>
        combined_formatted_ad_signals;
    for (const auto& [render_url, scoring_signals_json_obj] :
         combined_signals) {
      rapidjson::StringBuffer buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      scoring_signals_json_obj.Accept(writer);
      combined_formatted_ad_signals.try_emplace(render_url, std::move(buffer));
    }

    logger.vlog(2, "\nTrusted Scoring Signals Deserialize Time: ",
                ToInt64Microseconds((absl::Now() - start_parse_time)),
                " microseconds for ", combined_formatted_ad_signals.size(),
                " signals.");
    return combined_formatted_ad_signals;
  } else {
    return absl::InvalidArgumentError(kNoTrustedScoringSignals);
  }
}

std::shared_ptr<std::string> BuildAuctionConfig(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request) {
  return std::make_shared<std::string>(absl::StrCat(
      "{\"auctionSignals\": ",
      ((raw_request.auction_signals().empty()) ? "\"\""
                                               : raw_request.auction_signals()),
      ", ", "\"sellerSignals\": ",
      ((raw_request.seller_signals().empty()) ? "\"\""
                                              : raw_request.seller_signals()),
      "}"));
}

absl::StatusOr<rapidjson::Document> ParseAndGetScoreAdResponseJson(
    bool enable_adtech_code_logging, const std::string& response,
    const ContextLogger& logger) {
  PS_ASSIGN_OR_RETURN(rapidjson::Document document, ParseJsonString(response));
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
  rapidjson::Document response_obj;
  auto iterator = document.FindMember("response");
  if (iterator != document.MemberEnd() && iterator->value.IsObject()) {
    response_obj.CopyFrom(iterator->value, response_obj.GetAllocator());
  }
  return response_obj;
}

}  // namespace

ScoreAdsReactor::ScoreAdsReactor(
    const CodeDispatchClient& dispatcher, const ScoreAdsRequest* request,
    ScoreAdsResponse* response,
    std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    std::unique_ptr<AsyncReporter> async_reporter,
    const AuctionServiceRuntimeConfig& runtime_config)
    : CodeDispatchReactor<ScoreAdsRequest, ScoreAdsRequest::ScoreAdsRawRequest,
                          ScoreAdsResponse,
                          ScoreAdsResponse::ScoreAdsRawResponse>(
          dispatcher, request, response, key_fetcher_manager, crypto_client,
          runtime_config.encryption_enabled),
      benchmarking_logger_(std::move(benchmarking_logger)),
      async_reporter_(std::move(async_reporter)),
      enable_seller_debug_url_generation_(
          runtime_config.enable_seller_debug_url_generation),
      enable_adtech_code_logging_(runtime_config.enable_adtech_code_logging),
      enable_report_result_url_generation_(
          runtime_config.enable_report_result_url_generation),
      enable_report_win_url_generation_(
          runtime_config.enable_report_win_url_generation),
      roma_timeout_ms_(runtime_config.roma_timeout_ms) {
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::AuctionContextMap()->Remove(request_));
    return absl::OkStatus();
  }()) << "AuctionContextMap()->Get(request) should have been called";
}

ContextLogger::ContextMap ScoreAdsReactor::GetLoggingContext(
    const ScoreAdsRequest::ScoreAdsRawRequest& score_ads_request) {
  const auto& log_context = score_ads_request.log_context();
  return {
      {kGenerationId, log_context.generation_id()},
      {kSellerDebugId, log_context.adtech_debug_id()},
  };
}

void ScoreAdsReactor::Execute() {
  benchmarking_logger_->BuildInputBegin();
  logger_ = ContextLogger(GetLoggingContext(raw_request_));
  auto ads = raw_request_.ad_bids();
  if (ads.empty()) {
    Finish(::grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kNoAdsToScore));
    return;
  }

  absl::StatusOr<absl::flat_hash_map<std::string, rapidjson::StringBuffer>>
      scoring_signals = BuildTrustedScoringSignals(raw_request_, logger_);

  if (!scoring_signals.ok()) {
    Finish(FromAbslStatus(scoring_signals.status()));
    return;
  }

  std::shared_ptr<std::string> auction_config =
      BuildAuctionConfig(raw_request_);
  bool enable_debug_reporting = enable_seller_debug_url_generation_ &&
                                raw_request_.enable_debug_reporting();
  benchmarking_logger_->BuildInputEnd();
  while (!ads.empty()) {
    std::unique_ptr<AdWithBidMetadata> ad(ads.ReleaseLast());
    if (scoring_signals->contains(ad->render())) {
      DispatchRequest dispatch_request;
      dispatch_request = BuildScoreAdRequest(
          *ad, auction_config, raw_request_.publisher_hostname(),
          scoring_signals.value(), enable_debug_reporting, logger_,
          enable_adtech_code_logging_);
      ad_data_.emplace(dispatch_request.id, std::move(ad));
      dispatch_request.tags[kRomaTimeoutMs] = roma_timeout_ms_;
      dispatch_requests_.push_back(std::move(dispatch_request));
    }
  }

  if (dispatch_requests_.empty()) {
    Finish(::grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          kNoAdsWithValidScoringSignals));
    return;
  }
  absl::Time start_js_execution_time = absl::Now();
  auto status = dispatcher_.BatchExecute(
      dispatch_requests_,
      [this, start_js_execution_time](
          const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        int js_execution_time_ms =
            (absl::Now() - start_js_execution_time) / absl::Milliseconds(1);
        LogIfError(metric_context_->LogHistogram<metric::kJSExecutionDuration>(
            js_execution_time_ms));
        ScoreAdsCallback(result);
      });

  if (!status.ok()) {
    std::string original_request;
    TextFormat::PrintToString(raw_request_, &original_request);
    logger_.vlog(1, "Execution request failed for batch: ", original_request,
                 status.ToString(absl::StatusToStringMode::kWithEverything));
    LogIfError(
        metric_context_->LogUpDownCounter<metric::kJSExecutionErrorCount>(1));
    Finish(grpc::Status(grpc::StatusCode::INTERNAL, status.ToString()));
  }
}

ScoreAdsResponse::AdScore ParseScoreAdResponse(
    const rapidjson::Document& score_ad_resp) {
  ScoreAdsResponse::AdScore score_ads_response;
  auto desirability_itr =
      score_ad_resp.FindMember(kDesirabilityPropertyForScoreAd);
  if (desirability_itr == score_ad_resp.MemberEnd() ||
      !desirability_itr->value.IsNumber()) {
    score_ads_response.set_desirability(0.0);
  } else {
    score_ads_response.set_desirability(
        (double)desirability_itr->value.GetDouble());
  }

  auto component_auction_iter =
      score_ad_resp.FindMember(kAllowComponentAuctionPropertyForScoreAd);
  if (component_auction_iter == score_ad_resp.MemberEnd() ||
      !component_auction_iter->value.IsBool()) {
    score_ads_response.set_allow_component_auction(false);
  } else {
    score_ads_response.set_allow_component_auction(
        (double)component_auction_iter->value.GetBool());
  }

  auto debug_report_urls_itr =
      score_ad_resp.FindMember(kDebugReportUrlsPropertyForScoreAd);
  if (debug_report_urls_itr == score_ad_resp.MemberEnd()) {
    return score_ads_response;
  }
  DebugReportUrls debug_report_urls;
  if (debug_report_urls_itr->value.HasMember(
          kAuctionDebugWinUrlPropertyForScoreAd) &&
      debug_report_urls_itr->value[kAuctionDebugWinUrlPropertyForScoreAd]
          .IsString()) {
    debug_report_urls.set_auction_debug_win_url(
        debug_report_urls_itr->value[kAuctionDebugWinUrlPropertyForScoreAd]
            .GetString());
  }
  if (debug_report_urls_itr->value.HasMember(
          kAuctionDebugLossUrlPropertyForScoreAd) &&
      debug_report_urls_itr->value[kAuctionDebugLossUrlPropertyForScoreAd]
          .IsString()) {
    debug_report_urls.set_auction_debug_loss_url(
        debug_report_urls_itr->value[kAuctionDebugLossUrlPropertyForScoreAd]
            .GetString());
  }
  *score_ads_response.mutable_debug_report_urls() = debug_report_urls;
  return score_ads_response;
}

std::optional<ScoreAdsResponse::AdScore::AdRejectionReason>
ParseAdRejectionReason(const rapidjson::Document& score_ad_resp,
                       absl::string_view interest_group_owner,
                       absl::string_view interest_group_name,
                       const ContextLogger& logger) {
  std::optional<ScoreAdsResponse::AdScore::AdRejectionReason>
      ad_rejection_reason;
  auto reject_reason_itr =
      score_ad_resp.FindMember(kRejectReasonPropertyForScoreAd);
  if (reject_reason_itr == score_ad_resp.MemberEnd() ||
      !reject_reason_itr->value.IsString()) {
    return ad_rejection_reason;
  }
  std::string rejection_reason_str =
      score_ad_resp[kRejectReasonPropertyForScoreAd].GetString();
  SellerRejectionReason rejection_reason =
      ToSellerRejectionReason(rejection_reason_str);

  // We do not send rejection reasons if they are not available.
  if (rejection_reason ==
      SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE) {
    return ad_rejection_reason;
  }
  ScoreAdsResponse::AdScore::AdRejectionReason ad_rejection_reason_;
  ad_rejection_reason_.set_interest_group_owner(interest_group_owner);
  ad_rejection_reason_.set_interest_group_name(interest_group_name);
  ad_rejection_reason_.set_rejection_reason(rejection_reason);
  ad_rejection_reason = std::make_optional(ad_rejection_reason_);
  return ad_rejection_reason;
}

void ScoreAdsReactor::PerformReporting(
    const ScoreAdsResponse::AdScore& winning_ad_score) {
  std::vector<DispatchRequest> dispatch_requests;
  std::shared_ptr<std::string> auction_config =
      BuildAuctionConfig(raw_request_);
  DispatchRequest dispatch_request;
  BuyerReportingMetadata buyer_reporting_metadata = {
      .enable_report_win_url_generation = enable_report_win_url_generation_,
      .buyer_signals = raw_request_.per_buyer_signals().at(
          winning_ad_score.interest_group_owner()),
      .join_count = ad_data_.at(winning_ad_score.render()).get()->join_count(),
      .recency = ad_data_.at(winning_ad_score.render()).get()->recency(),
      .modeling_signals =
          ad_data_.at(winning_ad_score.render()).get()->modeling_signals()};
  dispatch_request = GetReportingDispatchRequest(
      winning_ad_score, raw_request_.publisher_hostname(),
      enable_adtech_code_logging_, auction_config, logger_,
      buyer_reporting_metadata);
  dispatch_request.tags[kRomaTimeoutMs] = roma_timeout_ms_;
  dispatch_requests.push_back(std::move(dispatch_request));
  auto status = dispatcher_.BatchExecute(
      dispatch_requests,
      [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        ReportingCallback(result);
      });

  if (!status.ok()) {
    std::string original_request;
    TextFormat::PrintToString(raw_request_, &original_request);
    logger_.vlog(
        1, "Reporting execution request failed for batch: ", original_request,
        status.ToString(absl::StatusToStringMode::kWithEverything));
    FinishWithOkStatus();
  }
}

// Handles the output of the code execution dispatch.
// Note that the dispatch response value is expected to be a json string
// conforming to the scoreAd function output described here:
// https://github.com/WICG/turtledove/blob/main/FLEDGE.md#23-scoring-bids
void ScoreAdsReactor::ScoreAdsCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  if (VLOG_IS_ON(2)) {
    for (const auto& dispatch_response : responses) {
      logger_.vlog(2, "ScoreAds V8 Response: ", dispatch_response.status());
      if (dispatch_response.ok()) {
        logger_.vlog(2, dispatch_response.value().resp);
      }
    }
  }
  benchmarking_logger_->HandleResponseBegin();

  // Saving the index of the most desirable ad allows us to only perform the
  // work of setting the overall response object once.
  int index_of_most_desirable_ad = 0;
  // List of all the scores and Ad's index in the response.
  absl::flat_hash_map<float, std::list<int>> score_ad_map;
  // Saving the desirability allows us to compare desirability between ads
  // without re-parsing the current most-desirable ad every time.
  float desirability_of_most_desirable_ad = std::numeric_limits<float>::min();
  // List of rejection reasons provided by seller.
  std::vector<ScoreAdsResponse::AdScore::AdRejectionReason>
      ad_rejection_reasons;

  std::optional<ScoreAdsResponse::AdScore> winning_ad;
  int total_bid_count = static_cast<int>(responses.size());
  int seller_rejected_bid_count = 0;
  LogIfError(metric_context_->AccumulateMetric<metric::kAuctionTotalBidsCount>(
      total_bid_count));
  for (int index = 0; index < responses.size(); index++) {
    if (responses[index].ok()) {
      absl::StatusOr<rapidjson::Document> response_json =
          ParseAndGetScoreAdResponseJson(enable_adtech_code_logging_,
                                         responses[index].value().resp,
                                         logger_);
      if (!response_json.ok()) {
        logger_.vlog(0, "Failed to parse response from Roma ",
                     response_json.status().ToString(
                         absl::StatusToStringMode::kWithEverything));
      }
      const AdWithBidMetadata* ad =
          ad_data_.at(responses[index].value().id).get();

      if (response_json.ok()) {
        ScoreAdsResponse::AdScore score_ads_response =
            ParseScoreAdResponse(*response_json);
        score_ads_response.set_interest_group_name(ad->interest_group_name());
        score_ads_response.set_interest_group_owner(ad->interest_group_owner());
        score_ads_response.set_buyer_bid(ad->bid());
        score_ads_response.set_ad_type(AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);
        // >= ensures that in the edge case where the most desirable ad's
        // desirability is float.min_val, it is still selected.
        if (score_ads_response.desirability() >=
            desirability_of_most_desirable_ad) {
          winning_ad = std::make_optional(score_ads_response);
          index_of_most_desirable_ad = index;
          desirability_of_most_desirable_ad = score_ads_response.desirability();
        }
        score_ad_map[score_ads_response.desirability()].push_back(index);
        ad_scores_.push_back(
            std::make_unique<ScoreAdsResponse::AdScore>(score_ads_response));
        // Parse Ad rejection reason and store only if it has value.
        const auto& ad_rejection_reason =
            ParseAdRejectionReason(*response_json, ad->interest_group_owner(),
                                   ad->interest_group_name(), logger_);
        if (ad_rejection_reason.has_value()) {
          ad_rejection_reasons.push_back(ad_rejection_reason.value());
          seller_rejected_bid_count += 1;
          LogIfError(
              metric_context_
                  ->AccumulateMetric<metric::kAuctionBidRejectedCount>(
                      1, ToSellerRejectionReasonString(
                             ad_rejection_reason.value().rejection_reason())));
        }
      } else {
        logger_.warn(
            "Invalid json output from code execution for interest group ",
            ad->interest_group_name(), ": ", responses[index].value().resp);
      }
    } else {
      logger_.warn("Invalid execution (possibly invalid input): ",
                   responses[index].status().ToString(
                       absl::StatusToStringMode::kWithEverything));
    }
  }
  LogIfError(metric_context_->LogHistogram<metric::kAuctionBidRejectedPercent>(
      (static_cast<double>(seller_rejected_bid_count)) / total_bid_count));
  // An Ad won.
  if (winning_ad.has_value()) {
    // Set the overall response for the winning winning_ad_with_bid.
    AdWithBidMetadata* winning_ad_with_bid =
        ad_data_.at(responses[index_of_most_desirable_ad].value().id).get();
    // Add the relevant fields.
    winning_ad->set_render(winning_ad_with_bid->render());
    winning_ad->mutable_component_renders()->Swap(
        winning_ad_with_bid->mutable_ad_components());
    std::vector<float> scores_list;
    // Logic to calculate the list of highest scoring other bids and
    // corresponding IG owners.
    for (const auto& entry : score_ad_map) {
      scores_list.push_back(entry.first);
    }
    // Sort the scores in descending order
    std::sort(scores_list.begin(), scores_list.end(),
              [](int a, int b) { return a > b; });
    // Add all the bids with the top 2 scores(excluding the winner and bids
    // with 0 score) and corresponding interest group owners to
    // ig_owner_highest_scoring_other_bids_map.
    for (int i = 0; i < 2 && i < scores_list.size(); i++) {
      if (scores_list.at(i) == 0) {
        break;
      }
      for (int current_index : score_ad_map.at(scores_list.at(i))) {
        if (index_of_most_desirable_ad != current_index) {
          const AdWithBidMetadata* ad =
              ad_data_.at(responses[current_index].value().id).get();
          winning_ad->mutable_ig_owner_highest_scoring_other_bids_map()
              ->try_emplace(ad->interest_group_owner(),
                            google::protobuf::ListValue());
          winning_ad->mutable_ig_owner_highest_scoring_other_bids_map()
              ->at(ad->interest_group_owner())
              .add_values()
              ->set_number_value(ad->bid());
        }
      }
    }

    winning_ad->mutable_ad_rejection_reasons()->Assign(
        ad_rejection_reasons.begin(), ad_rejection_reasons.end());

    PerformDebugReporting(winning_ad);
    *raw_response_.mutable_ad_score() = winning_ad.value();

    logger_.vlog(2, "ScoreAdsResponse:\n", response_->DebugString());
    if (!enable_report_result_url_generation_) {
      DCHECK(encryption_enabled_);
      EncryptResponse();
      benchmarking_logger_->HandleResponseEnd();
      FinishWithOkStatus();
      return;
    }
    PerformReporting(winning_ad.value());
  } else {
    LOG(WARNING) << "No ad was selected as most desirable";
    PerformDebugReporting(winning_ad);
    benchmarking_logger_->HandleResponseEnd();
    Finish(grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No ad was selected as most desirable"));
  }
}

void ScoreAdsReactor::ReportingCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  if (VLOG_IS_ON(2)) {
    for (const auto& dispatch_response : responses) {
      logger_.vlog(2, "Reporting V8 Response: ", dispatch_response.status());
      if (dispatch_response.ok()) {
        logger_.vlog(2, dispatch_response.value().resp);
      }
    }
  }
  for (const auto& response : responses) {
    if (response.ok()) {
      absl::StatusOr<ReportingResponse> reporting_response =
          ParseAndGetReportingResponse(enable_adtech_code_logging_,
                                       response.value().resp);
      if (!reporting_response.ok()) {
        logger_.vlog(0, "Failed to parse response from Roma ",
                     reporting_response.status().ToString(
                         absl::StatusToStringMode::kWithEverything));
        continue;
      }
      raw_response_.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_top_level_seller_reporting_urls()
          ->set_reporting_url(reporting_response.value()
                                  .report_result_response.report_result_url);
      raw_response_.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_buyer_reporting_urls()
          ->set_reporting_url(
              reporting_response.value().report_win_response.report_win_url);

      for (const auto& [event, interactionReportingUrl] :
           reporting_response.value()
               .report_result_response.interaction_reporting_urls) {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_top_level_seller_reporting_urls()
            ->mutable_interaction_reporting_urls()
            ->try_emplace(event, interactionReportingUrl);
      }
      for (const auto& [event, interactionReportingUrl] :
           reporting_response.value()
               .report_win_response.interaction_reporting_urls) {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_buyer_reporting_urls()
            ->mutable_interaction_reporting_urls()
            ->try_emplace(event, interactionReportingUrl);
      }
    } else {
      logger_.warn("Invalid execution (possibly invalid input): ",
                   response.status().ToString(
                       absl::StatusToStringMode::kWithEverything));
    }
  }

  logger_.vlog(2, "ReportingResponse:\n", response_->DebugString());
  DCHECK(encryption_enabled_);
  EncryptResponse();
  benchmarking_logger_->HandleResponseEnd();
  FinishWithOkStatus();
}

void ScoreAdsReactor::PerformDebugReporting(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score) {
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score);
  for (const auto& ad_score : ad_scores_) {
    if (ad_score->has_debug_report_urls()) {
      std::string debug_url;
      std::string ig_owner = ad_score->interest_group_owner();
      std::string ig_name = ad_score->interest_group_name();
      auto done_cb = [ig_owner,
                      ig_name](absl::StatusOr<absl::string_view> result) {
        if (result.ok()) {
          VLOG(2) << "Performed debug reporting for:" << ig_owner
                  << ", interest_group: " << ig_name;
        } else {
          VLOG(1) << "Error while performing debug reporting for:" << ig_owner
                  << ", interest_group: " << ig_name
                  << " ,status:" << result.status();
        }
      };
      if (ig_owner == post_auction_signals.winning_ig_owner &&
          ig_name == post_auction_signals.winning_ig_name) {
        debug_url = ad_score->debug_report_urls().auction_debug_win_url();
      } else {
        debug_url = ad_score->debug_report_urls().auction_debug_loss_url();
      }
      HTTPRequest http_request = CreateDebugReportingHttpRequest(
          debug_url, GetPlaceholderDataForInterestGroup(ig_owner, ig_name,
                                                        post_auction_signals));
      async_reporter_->DoReport(http_request, done_cb);
    }
  }
}

void ScoreAdsReactor::FinishWithOkStatus() {
  metric_context_->SetRequestSuccessful();
  Finish(grpc::Status::OK);
}

}  // namespace privacy_sandbox::bidding_auction_servers
