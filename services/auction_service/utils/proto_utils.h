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

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "include/rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "src/cpp/logger/request_context_impl.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

std::string MakeDeviceSignals(
    absl::string_view publisher_hostname,
    absl::string_view interest_group_owner, absl::string_view render_url,
    const google::protobuf::RepeatedPtrField<std::string>&
        ad_component_render_urls,
    absl::string_view top_level_seller, absl::string_view bid_currency);

std::shared_ptr<std::string> BuildAuctionConfig(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request);

absl::StatusOr<absl::flat_hash_map<std::string, rapidjson::StringBuffer>>
BuildTrustedScoringSignals(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
    server_common::log::ContextImpl& log_context);

void MayPopulateScoringSignalsForProtectedAppSignals(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
    absl::flat_hash_map<std::string, rapidjson::Document>& render_url_signals,
    absl::flat_hash_map<std::string, rapidjson::Document>& combined_signals,
    server_common::log::ContextImpl& log_context);

void MayLogScoreAdsInput(const std::vector<std::shared_ptr<std::string>>& input,
                         server_common::log::ContextImpl& log_context);

absl::StatusOr<rapidjson::Document> ParseAndGetScoreAdResponseJson(
    bool enable_ad_tech_code_logging, const std::string& response,
    server_common::log::ContextImpl& log_context);

std::optional<ScoreAdsResponse::AdScore::AdRejectionReason>
ParseAdRejectionReason(const rapidjson::Document& score_ad_resp,
                       absl::string_view interest_group_owner,
                       absl::string_view interest_group_name,
                       server_common::log::ContextImpl& log_context);

absl::StatusOr<ScoreAdsResponse::AdScore> ParseScoreAdResponse(
    const rapidjson::Document& score_ad_resp,
    int max_allowed_size_debug_url_chars,
    long max_allowed_size_all_debug_urls_chars,
    long current_all_debug_urls_chars, bool device_component_auction);

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
template <typename T>
absl::StatusOr<std::vector<std::shared_ptr<std::string>>> ScoreAdInput(
    const T& ad, std::shared_ptr<std::string> auction_config,
    const absl::flat_hash_map<std::string, rapidjson::StringBuffer>&
        scoring_signals,
    server_common::log::ContextImpl& log_context,
    bool enable_adtech_code_logging, bool enable_debug_reporting,
    absl::string_view device_signals) {
  std::vector<std::shared_ptr<std::string>> input(
      kArgSizeWithWrapper);  // ScoreAdArgs size

  // TODO: b/260265272
  std::string adMetadataAsJson;
  const auto& it = ad.ad().struct_value().fields().find("metadata");
  if (it != ad.ad().struct_value().fields().end()) {
    PS_RETURN_IF_ERROR(google::protobuf::util::MessageToJsonString(
        it->second, &adMetadataAsJson));
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
      std::make_shared<std::string>(device_signals);
  // This is only added to prevent errors in the score ad script, and
  // will always be an empty object.
  input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)] =
      std::make_shared<std::string>("{}");
  input[ScoreArgIndex(ScoreAdArgs::kFeatureFlags)] =
      std::make_shared<std::string>(GetFeatureFlagJson(
          enable_adtech_code_logging, enable_debug_reporting));

  MayLogScoreAdsInput(input, log_context);
  return input;
}

template <typename T>
absl::StatusOr<DispatchRequest> BuildScoreAdRequest(
    const T& ad, std::shared_ptr<std::string> auction_config,
    const absl::flat_hash_map<std::string, rapidjson::StringBuffer>&
        scoring_signals,
    const bool enable_debug_reporting,
    server_common::log::ContextImpl& log_context,
    const bool enable_adtech_code_logging, absl::string_view device_signals) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest score_ad_request;
  // TODO(b/250893468) Revisit dispatch id.
  score_ad_request.id = ad.render();
  // TODO(b/258790164) Update after code is fetched periodically.
  score_ad_request.version_string = "v1";
  score_ad_request.handler_name = DispatchHandlerFunctionWithSellerWrapper;

  PS_ASSIGN_OR_RETURN(score_ad_request.input,
                      ScoreAdInput(ad, auction_config, scoring_signals,
                                   log_context, enable_adtech_code_logging,
                                   enable_debug_reporting, device_signals));
  return score_ad_request;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_PROTO_UTILS_H_
