/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SERVICES_AUCTION_SERVICE_PROTO_UTILS_H_
#define SERVICES_AUCTION_SERVICE_PROTO_UTILS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "include/rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/loggers/request_log_context.h"
#include "src/util/status_macro/status_macros.h"

// Scoring signals are set to null when KV lookup fails. This is to maintain
// parity with Chrome.
constexpr absl::string_view kNullScoringSignalsJson = "null";

namespace privacy_sandbox::bidding_auction_servers {

struct ReportingIdsParamForBidMetadata {
  std::optional<absl::string_view> buyer_reporting_id;
  std::optional<absl::string_view> buyer_and_seller_reporting_id;
  std::optional<absl::string_view> selected_buyer_and_seller_reporting_id;
};

std::string MakeBidMetadata(
    absl::string_view publisher_hostname,
    absl::string_view interest_group_owner, absl::string_view render_url,
    const google::protobuf::RepeatedPtrField<std::string>&
        ad_component_render_urls,
    absl::string_view top_level_seller, absl::string_view bid_currency,
    const uint32_t seller_data_version,
    ReportingIdsParamForBidMetadata reporting_ids = {});

std::string MakeBidMetadataForTopLevelAuction(
    absl::string_view publisher_hostname,
    absl::string_view interest_group_owner, absl::string_view render_url,
    const google::protobuf::RepeatedPtrField<std::string>&
        ad_component_render_urls,
    absl::string_view component_seller, absl::string_view bid_currency,
    const uint32_t seller_data_version);

std::shared_ptr<std::string> BuildAuctionConfig(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request);

absl::StatusOr<absl::flat_hash_map<std::string, rapidjson::StringBuffer>>
BuildTrustedScoringSignals(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
    RequestLogContext& log_context,
    const bool require_scoring_signals_for_scoring);

void MayPopulateScoringSignalsForProtectedAppSignals(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
    absl::flat_hash_map<std::string, rapidjson::Document>& render_url_signals,
    absl::flat_hash_map<std::string, rapidjson::Document>& combined_signals,
    RequestLogContext& log_context);

void MayLogScoreAdsInput(const std::vector<std::shared_ptr<std::string>>& input,
                         RequestLogContext& log_context);

absl::StatusOr<rapidjson::Document> ParseAndGetScoreAdResponseJson(
    bool enable_ad_tech_code_logging, RequestLogContext& log_context,
    const rapidjson::Document& score_ads_wrapper_response);

ScoreAdsResponse::AdScore::AdRejectionReason BuildAdRejectionReason(
    absl::string_view interest_group_owner,
    absl::string_view interest_group_name,
    SellerRejectionReason seller_rejection_reason);

std::optional<ScoreAdsResponse::AdScore::AdRejectionReason>
ParseAdRejectionReason(const rapidjson::Document& score_ad_resp,
                       absl::string_view interest_group_owner,
                       absl::string_view interest_group_name,
                       RequestLogContext& log_context);

absl::StatusOr<ScoreAdsResponse::AdScore> ScoreAdResponseJsonToProto(
    const rapidjson::Document& score_ad_resp,
    int max_allowed_size_debug_url_chars,
    int64_t max_allowed_size_all_debug_urls_chars,
    bool device_component_auction, int64_t& current_all_debug_urls_chars);

constexpr int ScoreArgIndex(ScoreAdArgs arg) {
  return static_cast<std::underlying_type_t<ScoreAdArgs>>(arg);
}

/**
 * Maps GhostWinnerForTopLevelAuction object to AdWithBidMetadata object for
 * creating dispatch requests and performing post auction operations.
 * The fields related to bid in this object are invalidated after this method
 * is called since this method will try to move values rather than copy.
 */
std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>
MapKAnonGhostWinnerToAdWithBidMetadata(
    absl::string_view owner, absl::string_view ig_name,
    AuctionResult::KAnonGhostWinner::GhostWinnerForTopLevelAuction&
        ghost_winner);

/**
 * Maps component auction result object to AdWithBidMetadata object for
 * creating dispatch requests and performing post auction operations.
 * The fields related to bid in this object are invalidated after this method
 * is called since this method will try to move values rather than copy.
 */
std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>
MapAuctionResultToAdWithBidMetadata(AuctionResult& auction_result,
                                    bool k_anon_status = false);

/**
 * Maps component auction result object to ProtectedAppSignalsAdWithBidMetadata
 * object for creating dispatch requests and performing post auction operations.
 * The fields related to bid in this object are invalidated after this method
 * is called since this method will try to move values rather than copy.
 */
std::unique_ptr<
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata>
MapAuctionResultToProtectedAppSignalsAdWithBidMetadata(
    AuctionResult& auction_result, bool k_anon_status = false);

/**
 * Builds the ScoreAdInput, following the description here:
 * https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_api.md#scoreads
 * and here
 * https://github.com/WICG/turtledove/blob/main/FLEDGE.md#23-scoring-bids.
 *
 * NOTE: All inputs MUST be valid JSON, not just something Javascript would
 * accept. Property names need to be in quotes!  Additionally there are
 * considerations for how ROMA parses these -
 * null can passed
 * undefined doesn't work
 * empty string is mapped to undefined
 * all strings have to be valid JSON objects ({}) or escaped JSON Strings
 * (\"hello-world\").
 */
absl::StatusOr<DispatchRequest> BuildScoreAdRequest(
    absl::string_view ad_render_url, absl::string_view ad_metadata_json,
    absl::string_view scoring_signals, float ad_bid,
    const std::shared_ptr<std::string>& auction_config,
    absl::string_view bid_metadata, RequestLogContext& log_context,
    const bool enable_adtech_code_logging, const bool enable_debug_reporting,
    absl::string_view code_version);

/**
 * Builds ScoreAdInput with AdWithBid or ProtectedAppSignalsAdWithBid.
 */
template <typename T>
absl::StatusOr<DispatchRequest> BuildScoreAdRequest(
    const T& ad, const std::shared_ptr<std::string>& auction_config,
    absl::string_view scoring_signals, const bool enable_debug_reporting,
    RequestLogContext& log_context, const bool enable_adtech_code_logging,
    absl::string_view bid_metadata, absl::string_view code_version) {
  std::string ad_object_json;
  if (ad.ad().has_struct_value()) {
    PS_RETURN_IF_ERROR(
        google::protobuf::util::MessageToJsonString(ad.ad(), &ad_object_json));
  } else {
    ad_object_json = ad.ad().string_value();
  }

  return BuildScoreAdRequest(ad.render(), ad_object_json, scoring_signals,
                             ad.bid(), auction_config, bid_metadata,
                             log_context, enable_adtech_code_logging,
                             enable_debug_reporting, code_version);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_PROTO_UTILS_H_
