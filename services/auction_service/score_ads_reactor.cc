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
#include "services/common/util/debug_reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/status_macros.h"
#include "services/common/util/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;

constexpr char DispatchHandlerFunctionName[] = "scoreAd";
constexpr char DispatchWrapperHandlerFunctionName[] = "scoreAdWrapper";
constexpr char kAdComponentRenderUrlsProperty[] = "adComponentRenderUrls";
constexpr char kRenderUrlsPropertyForKVResponse[] = "renderUrls";
constexpr char kRenderUrlsPropertyForScoreAd[] = "renderUrl";
constexpr char kRomaTimeoutMs[] = "TimeoutMs";

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
    const ContextLogger& logger) {
  std::vector<std::shared_ptr<std::string>> input(6);  // ScoreAdArgs size

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
                            ad.render(), ad.ad_component_render()));
  // This is only added to prevent errors in the score ad script, and
  // will always be an empty object.
  input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)] =
      std::make_shared<std::string>("{}");
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
    const bool enable_debug_reporting, const ContextLogger& logger) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest score_ad_request;
  // TODO(b/250893468) Revisit dispatch id.
  score_ad_request.id = ad.render();
  // TODO(b/258790164) Update after code is fetched periodically.
  score_ad_request.version_num = 1;
  if (enable_debug_reporting) {
    score_ad_request.handler_name = DispatchWrapperHandlerFunctionName;
  } else {
    score_ad_request.handler_name = DispatchHandlerFunctionName;
  }
  score_ad_request.input = ScoreAdInput(ad, auction_config, publisher_hostname,
                                        scoring_signals, logger);
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
    for (const auto& ad_component_render_url :
         ad_with_bid.ad_component_render()) {
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

  for (const auto& ad_component_render_url :
       ad_with_bid.ad_component_render()) {
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
}  // namespace

ScoreAdsReactor::ScoreAdsReactor(
    const CodeDispatchClient& dispatcher, const ScoreAdsRequest* request,
    ScoreAdsResponse* response,
    std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    std::unique_ptr<AsyncReporter> async_reporter,
    const AuctionServiceRuntimeConfig& runtime_config)
    : CodeDispatchReactor<ScoreAdsRequest, ScoreAdsRequest_ScoreAdsRawRequest,
                          ScoreAdsResponse>(dispatcher, request, response,
                                            key_fetcher_manager, crypto_client,
                                            runtime_config.encryption_enabled),
      benchmarking_logger_(std::move(benchmarking_logger)),
      async_reporter_(std::move(async_reporter)),
      enable_seller_debug_url_generation_(
          runtime_config.enable_seller_debug_url_generation),
      roma_timeout_ms_(runtime_config.roma_timeout_ms),
      logger_(GetLoggingContext(*request)) {
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::AuctionContextMap()->Remove(request_));
    return absl::OkStatus();
  }()) << "AuctionContextMap()->Get(request) should have been called";
}

ContextLogger::ContextMap ScoreAdsReactor::GetLoggingContext(
    const ScoreAdsRequest& score_ads_request) {
  const auto& log_context = score_ads_request.raw_request().log_context();
  return {
      {kGenerationId, log_context.generation_id()},
      {kSellerDebugId, log_context.adtech_debug_id()},
  };
}

void ScoreAdsReactor::Execute() {
  benchmarking_logger_->BuildInputBegin();
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
          scoring_signals.value(), enable_debug_reporting, logger_);
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

  auto status = dispatcher_.BatchExecute(
      dispatch_requests_,
      [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        ScoreAdsCallback(result);
      });

  if (!status.ok()) {
    std::string original_request;
    TextFormat::PrintToString(raw_request_, &original_request);
    logger_.vlog(1, "Execution request failed for batch: ", original_request,
                 status.ToString(absl::StatusToStringMode::kWithEverything));
    Finish(grpc::Status(grpc::StatusCode::INTERNAL, status.ToString()));
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
  response_->mutable_raw_response();
  benchmarking_logger_->HandleResponseBegin();

  // Saving the index of the most desirable ad allows us to only perform the
  // work of setting the overall response object once.
  int index_of_most_desirable_ad = 0;
  // List of all the scores and Ad's index in the response.
  absl::flat_hash_map<float, std::list<int>> score_ad_map;
  // Saving the desirability allows us to compare desirability between ads
  // without re-parsing the current most-desirable ad every time.
  float desirability_of_most_desirable_ad = std::numeric_limits<float>::min();
  std::optional<ScoreAdsResponse::AdScore> winning_ad;
  for (int index = 0; index < responses.size(); index++) {
    if (responses[index].ok()) {
      ScoreAdsResponse::AdScore score_ads_response;
      // Set the fields that the response includes automatically.
      google::protobuf::json::ParseOptions parse_options;
      parse_options.ignore_unknown_fields = true;
      auto valid = google::protobuf::util::JsonStringToMessage(
          responses[index].value().resp, &score_ads_response, parse_options);
      const AdWithBidMetadata* ad =
          ad_data_.at(responses[index].value().id).get();
      if (valid.ok()) {
        score_ads_response.set_interest_group_name(ad->interest_group_name());
        score_ads_response.set_interest_group_owner(ad->interest_group_owner());
        score_ads_response.set_buyer_bid(ad->bid());
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
  // An Ad won.
  if (winning_ad.has_value()) {
    // Set the overall response for the winning winning_ad_with_bid.
    AdWithBidMetadata* winning_ad_with_bid =
        ad_data_.at(responses[index_of_most_desirable_ad].value().id).get();
    // Add the relevant fields.
    winning_ad->set_render(winning_ad_with_bid->render());
    winning_ad->mutable_component_renders()->Swap(
        winning_ad_with_bid->mutable_ad_component_render());
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
    PerformDebugReporting(winning_ad);
    *response_->mutable_raw_response()->mutable_ad_score() = winning_ad.value();
    if (encryption_enabled_) {
      EncryptResponse();
    }

    logger_.vlog(2, "ScoreAdsResponse:\n", response_->DebugString());
    benchmarking_logger_->HandleResponseEnd();
    Finish(grpc::Status(grpc::Status::OK));
  } else {
    LOG(WARNING) << "No ad was selected as most desirable";
    PerformDebugReporting(winning_ad);
    benchmarking_logger_->HandleResponseEnd();
    Finish(grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No ad was selected as most desirable"));
  }
}

void ScoreAdsReactor::PerformDebugReporting(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score) {
  std::unique_ptr<PostAuctionSignals> post_auction_signals =
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
      if (ig_owner == post_auction_signals->winning_ig_owner &&
          ig_name == post_auction_signals->winning_ig_name) {
        debug_url = ad_score->debug_report_urls().auction_debug_win_url();
      } else {
        debug_url = ad_score->debug_report_urls().auction_debug_loss_url();
      }
      HTTPRequest http_request = CreateDebugReportingHttpRequest(
          debug_url, GetPlaceholderDataForInterestGroupOwner(
                         ig_owner, *post_auction_signals));
      async_reporter_->DoReport(http_request, done_cb);
    }
  }
}
}  // namespace privacy_sandbox::bidding_auction_servers
