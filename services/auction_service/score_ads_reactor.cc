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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_replace.h"
#include "rapidjson/document.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/reporting/buyer/buyer_reporting_helper.h"
#include "services/auction_service/reporting/buyer/pa_buyer_reporting_manager.h"
#include "services/auction_service/reporting/buyer/pas_buyer_reporting_manager.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/reporting/seller/component_seller_reporting_manager.h"
#include "services/auction_service/reporting/seller/seller_reporting_manager.h"
#include "services/auction_service/reporting/seller/top_level_seller_reporting_manager.h"
#include "services/auction_service/utils/proto_utils.h"
#include "services/common/util/auction_scope_util.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;
using ::google::protobuf::RepeatedPtrField;
using HighestScoringOtherBidsMap =
    ::google::protobuf::Map<std::string, google::protobuf::ListValue>;
using server_common::log::PS_VLOG_IS_ON;

constexpr int kBytesMultiplyer = 1024;

inline void MayVlogRomaResponses(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses,
    RequestLogContext& log_context) {
  if (PS_VLOG_IS_ON(2)) {
    for (const auto& dispatch_response : responses) {
      PS_VLOG(kDispatch, log_context)
          << "ScoreAds V8 Response: " << dispatch_response.status();
      if (dispatch_response.ok()) {
        PS_VLOG(kDispatch, log_context) << dispatch_response->resp;
      }
    }
  }
}

inline void LogWarningForBadResponse(
    const absl::Status& status, const DispatchResponse& response,
    const AdWithBidMetadata* ad_with_bid_metadata,
    RequestLogContext& log_context) {
  PS_LOG(ERROR, log_context) << "Failed to parse response from Roma ",
      status.ToString(absl::StatusToStringMode::kWithEverything);
  if (ad_with_bid_metadata) {
    PS_LOG(WARNING, log_context)
        << "Invalid json output from code execution for interest group "
        << ad_with_bid_metadata->interest_group_name() << ": " << response.resp;
  } else {
    PS_LOG(WARNING, log_context)
        << "Invalid json output from code execution for protected app signals "
           "ad: "
        << response.resp;
  }
}

inline void UpdateHighestScoringOtherBidMap(
    float bid, absl::string_view owner,
    HighestScoringOtherBidsMap& highest_scoring_other_bids_map) {
  highest_scoring_other_bids_map.try_emplace(owner,
                                             google::protobuf::ListValue());
  highest_scoring_other_bids_map.at(owner).add_values()->set_number_value(bid);
}

long DebugReportUrlsLength(const ScoreAdsResponse::AdScore& ad_score) {
  if (!ad_score.has_debug_report_urls()) {
    return 0;
  }
  return ad_score.debug_report_urls().auction_debug_win_url().length() +
         ad_score.debug_report_urls().auction_debug_loss_url().length();
}

// If the bid's original currency matches the seller currency, the
// incomingBidInSellerCurrency must be unchanged by scoreAd(). If it is
// changed, reject the bid.
bool IsIncomingBidInSellerCurrencyIllegallyModified(
    absl::string_view seller_currency, absl::string_view ad_with_bid_currency,
    float incoming_bid_in_seller_currency, float buyer_bid) {
  return  // First check if the original currency matches the seller currency.
      !seller_currency.empty() && !ad_with_bid_currency.empty() &&
      seller_currency == ad_with_bid_currency &&
      // Then check that the incomingBidInSellerCurrency was set (non-zero).
      (incoming_bid_in_seller_currency > kCurrencyFloatComparisonEpsilon) &&
      // Only now do we check if it was modified.
      (fabsf(incoming_bid_in_seller_currency - buyer_bid) >
       kCurrencyFloatComparisonEpsilon);
}

// Generates GetComponentReportingDataInAuctionResult based on auction_result.
ComponentReportingDataInAuctionResult GetComponentReportingDataInAuctionResult(
    AuctionResult& auction_result) {
  ComponentReportingDataInAuctionResult
      component_reporting_data_in_auction_result;
  component_reporting_data_in_auction_result.component_seller =
      auction_result.auction_params().component_seller();
  component_reporting_data_in_auction_result.win_reporting_urls =
      std::move(*auction_result.mutable_win_reporting_urls());
  component_reporting_data_in_auction_result.buyer_reporting_id =
      auction_result.buyer_reporting_id();
  return component_reporting_data_in_auction_result;
}

// Populates reporting urls of component level seller and buyer in top level
// seller's ScoreAdResponse.
void PopulateComponentReportingUrlsInTopLevelResponse(
    absl::string_view dispatch_id,
    ScoreAdsResponse::ScoreAdsRawResponse& raw_response_,
    absl::flat_hash_map<std::string, ComponentReportingDataInAuctionResult>&
        component_level_reporting_data_) {
  if (auto ad_it = component_level_reporting_data_.find(dispatch_id);
      ad_it != component_level_reporting_data_.end()) {
    auto* seller_reporting_urls =
        ad_it->second.win_reporting_urls
            .mutable_component_seller_reporting_urls();
    raw_response_.mutable_ad_score()
        ->mutable_win_reporting_urls()
        ->mutable_component_seller_reporting_urls()
        ->set_reporting_url(
            std::move(*seller_reporting_urls->mutable_reporting_url()));
    for (auto& [event, url] :
         *seller_reporting_urls->mutable_interaction_reporting_urls()) {
      raw_response_.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_component_seller_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, std::move(url));
    }

    auto* component_buyer_reporting_urls =
        ad_it->second.win_reporting_urls.mutable_buyer_reporting_urls();
    raw_response_.mutable_ad_score()
        ->mutable_win_reporting_urls()
        ->mutable_buyer_reporting_urls()
        ->set_reporting_url(std::move(
            *component_buyer_reporting_urls->mutable_reporting_url()));
    for (auto& [event, url] : *component_buyer_reporting_urls
                                   ->mutable_interaction_reporting_urls()) {
      raw_response_.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_buyer_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, std::move(url));
    }
  }
}

// If this is a component auction, and a seller currency is set,
// and a modified bid is set, and a currency for that modified bid is set,
// it must match the seller currency. If not, reject the bid.
inline bool IsBidCurrencyMismatched(AuctionScope auction_scope,
                                    absl::string_view seller_currency,
                                    float modified_bid,
                                    absl::string_view modified_bid_currency) {
  return (auction_scope ==
              AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
          auction_scope ==
              AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) &&
         !seller_currency.empty() && modified_bid > 0.0f &&
         !modified_bid_currency.empty() &&
         modified_bid_currency != seller_currency;
}

// If this is a component auction and the modified bid was not set,
// the original buyer bid (and its currency) will be used.
// Returns whether the bid was updated.
bool CheckAndUpdateModifiedBid(AuctionScope auction_scope, float buyer_bid,
                               absl::string_view ad_with_bid_currency,
                               ScoreAdsResponse::AdScore* ad_score) {
  if ((auction_scope ==
           AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
       auction_scope ==
           AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) &&
      ad_score->bid() <= 0.0f) {
    if (buyer_bid > 0.0f) {
      ad_score->set_bid(buyer_bid);
      if (!ad_with_bid_currency.empty()) {
        ad_score->set_bid_currency(ad_with_bid_currency);
      } else {
        ad_score->clear_bid_currency();
      }
      return true;
    }
  }
  return false;
}

void SetSellerReportingUrlsInResponse(
    ReportResultResponse report_result_response,
    ScoreAdsResponse::ScoreAdsRawResponse& raw_response) {
  auto top_level_reporting_urls =
      raw_response.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_top_level_seller_reporting_urls();
  top_level_reporting_urls->set_reporting_url(
      std::move(report_result_response.report_result_url));
  auto mutable_interaction_url =
      top_level_reporting_urls->mutable_interaction_reporting_urls();
  for (const auto& [event, interactionReportingUrl] :
       report_result_response.interaction_reporting_urls) {
    mutable_interaction_url->try_emplace(event, interactionReportingUrl);
  }
}

void SetSellerReportingUrlsInResponseForComponentAuction(
    ReportResultResponse report_result_response,
    ScoreAdsResponse::ScoreAdsRawResponse& raw_response) {
  auto component_seller_reporting_urls =
      raw_response.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_component_seller_reporting_urls();
  component_seller_reporting_urls->set_reporting_url(
      std::move(report_result_response.report_result_url));
  auto mutable_interaction_url =
      component_seller_reporting_urls->mutable_interaction_reporting_urls();
  for (const auto& [event, interactionReportingUrl] :
       report_result_response.interaction_reporting_urls) {
    mutable_interaction_url->try_emplace(event, interactionReportingUrl);
  }
}
}  // namespace

void ScoringData::UpdateWinner(int index,
                               const ScoreAdsResponse::AdScore& ad_score) {
  winning_ad = ad_score;
  index_of_most_desirable_ad = index;
  desirability_of_most_desirable_ad = ad_score.desirability();
}

ScoreAdsReactor::ScoreAdsReactor(
    grpc::CallbackServerContext* context, V8DispatchClient& dispatcher,
    const ScoreAdsRequest* request, ScoreAdsResponse* response,
    std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const AsyncReporter* async_reporter,
    const AuctionServiceRuntimeConfig& runtime_config)
    : CodeDispatchReactor<ScoreAdsRequest, ScoreAdsRequest::ScoreAdsRawRequest,
                          ScoreAdsResponse,
                          ScoreAdsResponse::ScoreAdsRawResponse>(
          request, response, key_fetcher_manager, crypto_client),
      context_(context),
      dispatcher_(dispatcher),
      benchmarking_logger_(std::move(benchmarking_logger)),
      async_reporter_(*async_reporter),
      enable_seller_debug_url_generation_(
          runtime_config.enable_seller_debug_url_generation),
      roma_timeout_ms_(runtime_config.roma_timeout_ms),
      log_context_(GetLoggingContext(raw_request_),
                   raw_request_.consented_debug_config(),
                   [this]() { return raw_response_.mutable_debug_info(); }),
      enable_adtech_code_logging_(log_context_.is_consented()),
      enable_report_result_url_generation_(
          runtime_config.enable_report_result_url_generation),
      enable_protected_app_signals_(
          runtime_config.enable_protected_app_signals),
      enable_report_win_input_noising_(
          runtime_config.enable_report_win_input_noising),
      max_allowed_size_debug_url_chars_(
          runtime_config.max_allowed_size_debug_url_bytes),
      max_allowed_size_all_debug_urls_chars_(
          kBytesMultiplyer * runtime_config.max_allowed_size_all_debug_urls_kb),
      auction_scope_(GetAuctionScope(raw_request_)),
      code_version_(runtime_config.default_code_version),
      enable_report_win_url_generation_(
          runtime_config.enable_report_win_url_generation &&
          auction_scope_ !=
              AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER),
      enable_seller_and_buyer_code_isolation_(
          runtime_config.enable_seller_and_buyer_udf_isolation) {
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::AuctionContextMap()->Remove(request_));
    if (log_context_.is_consented()) {
      metric_context_->SetConsented(raw_request_.log_context().generation_id());
    }
    return absl::OkStatus();
  }()) << "AuctionContextMap()->Get(request) should have been called";
}

absl::btree_map<std::string, std::string> ScoreAdsReactor::GetLoggingContext(
    const ScoreAdsRequest::ScoreAdsRawRequest& score_ads_request) {
  const auto& log_context = score_ads_request.log_context();
  return {{kGenerationId, log_context.generation_id()},
          {kSellerDebugId, log_context.adtech_debug_id()}};
}

absl::Status ScoreAdsReactor::PopulateTopLevelAuctionDispatchRequests(
    bool enable_debug_reporting,
    const std::shared_ptr<std::string>& auction_config,
    google::protobuf::RepeatedPtrField<AuctionResult>&
        component_auction_results) {
  absl::string_view generation_id;
  PS_VLOG(8, log_context_) << __func__;
  for (auto& auction_result : component_auction_results) {
    if (auction_result.is_chaff() ||
        auction_result.auction_params().component_seller().empty()) {
      continue;
    }
    if (generation_id.empty()) {
      generation_id =
          auction_result.auction_params().ciphertext_generation_id();
    } else if (generation_id !=
               auction_result.auction_params().ciphertext_generation_id()) {
      // Ignore auction result for different generation id.
      return absl::Status(
          absl::StatusCode::kInvalidArgument,
          "Mismatched generation ids in component auction results.");
    }
    auto dispatch_request = BuildScoreAdRequest(
        auction_result.ad_render_url(), auction_result.ad_metadata(),
        /*scoring_signals = */ "{}", auction_result.bid(), auction_config,
        MakeBidMetadataForTopLevelAuction(
            raw_request_.publisher_hostname(),
            auction_result.interest_group_owner(),
            auction_result.ad_render_url(),
            auction_result.ad_component_render_urls(),
            auction_result.auction_params().component_seller(),
            auction_result.bid_currency()),
        log_context_, enable_adtech_code_logging_, enable_debug_reporting,
        code_version_);
    if (!dispatch_request.ok()) {
      PS_VLOG(kDispatch, log_context_)
          << "Failed to create scoring request for protected audience: "
          << dispatch_request.status();
      continue;
    }
    auto [unused_it_reporting, reporting_inserted] =
        component_level_reporting_data_.emplace(
            dispatch_request->id,
            GetComponentReportingDataInAuctionResult(auction_result));
    if (!reporting_inserted) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Could not insert reporting data for Protected Audience ScoreAd "
             "Request id "
             "conflict detected: "
          << dispatch_request->id;
      LogIfError(
          metric_context_
              ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                  1, metric::kAuctionScoreAdsFailedToInsertDispatchRequest));
      continue;
    }
    // Map all fields from a component auction result to a
    // AdWithBidMetadata used in this reactor. This way all the parsing
    // and result handling logic for single seller auctions can be
    // re-used for top-level auctions.
    auto [unused_it, inserted] =
        ad_data_.emplace(dispatch_request->id,
                         MapAuctionResultToAdWithBidMetadata(auction_result));
    if (!inserted) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Protected Audience ScoreAd Request id "
             "conflict detected: "
          << dispatch_request->id;
      LogIfError(
          metric_context_
              ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                  1, metric::kAuctionScoreAdsFailedToInsertDispatchRequest));
      continue;
    }

    dispatch_request->tags[kRomaTimeoutMs] = roma_timeout_ms_;
    dispatch_requests_.push_back(*std::move(dispatch_request));
  }
  return absl::OkStatus();
}

void ScoreAdsReactor::PopulateProtectedAudienceDispatchRequests(
    bool enable_debug_reporting,
    const absl::flat_hash_map<std::string, rapidjson::StringBuffer>&
        scoring_signals,
    const std::shared_ptr<std::string>& auction_config,
    google::protobuf::RepeatedPtrField<AdWithBidMetadata>& ads) {
  while (!ads.empty()) {
    std::unique_ptr<AdWithBidMetadata> ad(ads.ReleaseLast());
    if (scoring_signals.contains(ad->render())) {
      auto dispatch_request = BuildScoreAdRequest(
          *ad, auction_config, scoring_signals, enable_debug_reporting,
          log_context_, enable_adtech_code_logging_,
          MakeBidMetadata(raw_request_.publisher_hostname(),
                          ad->interest_group_owner(), ad->render(),
                          ad->ad_components(), raw_request_.top_level_seller(),
                          ad->bid_currency()),
          code_version_);
      if (!dispatch_request.ok()) {
        PS_VLOG(kNoisyWarn, log_context_)
            << "Failed to create scoring request for protected audience: "
            << dispatch_request.status();
        continue;
      }
      auto [unused_it, inserted] =
          ad_data_.emplace(dispatch_request->id, std::move(ad));
      if (!inserted) {
        PS_VLOG(kNoisyWarn, log_context_)
            << "Protected Audience ScoreAd Request id "
               "conflict detected: "
            << dispatch_request->id;
        continue;
      }

      dispatch_request->tags[kRomaTimeoutMs] = roma_timeout_ms_;
      dispatch_requests_.push_back(*std::move(dispatch_request));
    }
  }
}

void ScoreAdsReactor::MayPopulateProtectedAppSignalsDispatchRequests(
    bool enable_debug_reporting,
    const absl::flat_hash_map<std::string, rapidjson::StringBuffer>&
        scoring_signals,
    const std::shared_ptr<std::string>& auction_config,
    RepeatedPtrField<ProtectedAppSignalsAdWithBidMetadata>&
        protected_app_signals_ad_bids) {
  PS_VLOG(8, log_context_) << __func__;
  while (!protected_app_signals_ad_bids.empty()) {
    std::unique_ptr<ProtectedAppSignalsAdWithBidMetadata> pas_ad_with_bid(
        protected_app_signals_ad_bids.ReleaseLast());
    if (!scoring_signals.contains(pas_ad_with_bid->render())) {
      PS_VLOG(5, log_context_)
          << "Skipping protected app signals ad since render "
             "URL is not found in the scoring signals: "
          << pas_ad_with_bid->render();
      continue;
    }

    auto dispatch_request = BuildScoreAdRequest(
        *pas_ad_with_bid, auction_config, scoring_signals,
        enable_debug_reporting, log_context_, enable_adtech_code_logging_,
        MakeBidMetadata(
            raw_request_.publisher_hostname(), pas_ad_with_bid->owner(),
            pas_ad_with_bid->render(), GetEmptyAdComponentRenderUrls(),
            raw_request_.top_level_seller(), pas_ad_with_bid->bid_currency()),
        code_version_);
    if (!dispatch_request.ok()) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "Failed to create scoring request for protected app signals ad: "
          << dispatch_request.status();
      continue;
    }

    auto [unused_it, inserted] = protected_app_signals_ad_data_.emplace(
        dispatch_request->id, std::move(pas_ad_with_bid));
    if (!inserted) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "ProtectedAppSignals ScoreAd Request id conflict detected: "
          << dispatch_request->id;
      continue;
    }

    dispatch_request->tags[kRomaTimeoutMs] = roma_timeout_ms_;
    dispatch_requests_.push_back(*std::move(dispatch_request));
  }
}

void ScoreAdsReactor::Execute() {
  if (enable_cancellation_ && context_->IsCancelled()) {
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  PS_VLOG(kEncrypted, log_context_)
      << "Encrypted ScoreAdsRequest exported in EventMessage";
  log_context_.SetEventMessageField(*request_);
  PS_VLOG(kPlain, log_context_)
      << "ScoreAdsRawRequest exported in EventMessage";
  log_context_.SetEventMessageField(raw_request_);
  if (!raw_request_.protected_app_signals_ad_bids().empty() &&
      !enable_protected_app_signals_) {
    PS_VLOG(kNoisyWarn, log_context_) << "Found PAS bids even when feature is "
                                      << "disabled, discarding these bids";
    raw_request_.clear_protected_app_signals_ad_bids();
  }

  if (auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER &&
      !raw_request_.protected_app_signals_ad_bids().empty()) {
    // This path should be unreachable from SFE.
    // Component PA and PAS auctions cannot be done together for now.
    PS_LOG(ERROR, log_context_)
        << "Finishing RPC: " << kDeviceComponentAuctionWithPAS;
    FinishWithStatus(::grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                    kDeviceComponentAuctionWithPAS));
    return;
  }

  benchmarking_logger_->BuildInputBegin();
  std::shared_ptr<std::string> auction_config =
      BuildAuctionConfig(raw_request_);
  bool enable_debug_reporting = enable_seller_debug_url_generation_ &&
                                raw_request_.enable_debug_reporting();
  if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    if (raw_request_.component_auction_results_size() == 0) {
      // Internal error so that this is not propagated back to ad server.
      FinishWithStatus(::grpc::Status(grpc::StatusCode::INTERNAL,
                                      kNoValidComponentAuctions));
      return;
    }
    absl::Status top_level_req_status = PopulateTopLevelAuctionDispatchRequests(
        enable_debug_reporting, auction_config,
        *raw_request_.mutable_component_auction_results());
    if (!top_level_req_status.ok()) {
      // Invalid Argument error so that this is propagated back to ad server.
      FinishWithStatus(::grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                      top_level_req_status.message().data()));
      return;
    }
  } else {
    auto ads = raw_request_.ad_bids();
    auto protected_app_signals_ad_bids =
        raw_request_.protected_app_signals_ad_bids();
    absl::StatusOr<absl::flat_hash_map<std::string, rapidjson::StringBuffer>>
        scoring_signals =
            BuildTrustedScoringSignals(raw_request_, log_context_);

    if (!scoring_signals.ok()) {
      PS_LOG(ERROR, log_context_) << "No scoring signals found, finishing RPC: "
                                  << scoring_signals.status();
      FinishWithStatus(server_common::FromAbslStatus(scoring_signals.status()));
      return;
    }
    PopulateProtectedAudienceDispatchRequests(
        enable_debug_reporting, *scoring_signals, auction_config, ads);
    MayPopulateProtectedAppSignalsDispatchRequests(
        enable_debug_reporting, *scoring_signals, auction_config,
        protected_app_signals_ad_bids);
  }

  benchmarking_logger_->BuildInputEnd();

  if (dispatch_requests_.empty()) {
    // Internal error so that this is not propagated back to ad server.
    FinishWithStatus(::grpc::Status(grpc::StatusCode::INTERNAL,
                                    kNoAdsWithValidScoringSignals));
    return;
  }
  absl::Time start_js_execution_time = absl::Now();
  auto status = dispatcher_.BatchExecute(
      dispatch_requests_,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this, start_js_execution_time, enable_debug_reporting](
              const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            int js_execution_time_ms =
                (absl::Now() - start_js_execution_time) / absl::Milliseconds(1);
            LogIfError(
                metric_context_->LogHistogram<metric::kJSExecutionDuration>(
                    js_execution_time_ms));
            ScoreAdsCallback(result, enable_debug_reporting);
          },
          [this]() {
            FinishWithStatus(
                grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
          }));

  if (!status.ok()) {
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                       1, metric::kAuctionScoreAdsFailedToDispatchCode));
    PS_LOG(ERROR, log_context_)
        << "Execution request failed for batch: " << raw_request_.DebugString()
        << status.ToString(absl::StatusToStringMode::kWithEverything);
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::UNKNOWN, status.ToString()));
  }
}

void ScoreAdsReactor::InitializeBuyerReportingDispatchRequestData(
    const ScoreAdsResponse::AdScore& winning_ad_score) {
  buyer_reporting_dispatch_request_data_.buyer_signals =
      raw_request_.per_buyer_signals().at(
          winning_ad_score.interest_group_owner());
  buyer_reporting_dispatch_request_data_.seller = raw_request_.seller();
  buyer_reporting_dispatch_request_data_.interest_group_name =
      winning_ad_score.interest_group_name();
  buyer_reporting_dispatch_request_data_.buyer_origin =
      winning_ad_score.interest_group_owner();
  buyer_reporting_dispatch_request_data_.made_highest_scoring_other_bid =
      post_auction_signals_.made_highest_scoring_other_bid;
}

void ScoreAdsReactor::PerformReportingWithSellerAndBuyerCodeIsolation(
    const ScoreAdsResponse::AdScore& winning_ad_score, absl::string_view id) {
  reporting_dispatch_request_config_ = {
      .enable_report_win_url_generation = enable_report_win_url_generation_,
      .enable_protected_app_signals = enable_protected_app_signals_,
      .enable_report_win_input_noising = enable_report_win_input_noising_,
      .enable_adtech_code_logging = enable_adtech_code_logging_};
  buyer_reporting_dispatch_request_data_.auction_config =
      BuildAuctionConfig(raw_request_);

  if (auto ad_it = ad_data_.find(id); ad_it != ad_data_.end()) {
    if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
      post_auction_signals_ =
          GeneratePostAuctionSignalsForTopLevelSeller(winning_ad_score);
      DispatchReportResultRequestForTopLevelAuction(winning_ad_score, id);
      return;
    }
    post_auction_signals_ = GeneratePostAuctionSignals(
        winning_ad_score, raw_request_.seller_currency());
    InitializeBuyerReportingDispatchRequestData(winning_ad_score);

    const auto& ad = ad_it->second;
    buyer_reporting_dispatch_request_data_.join_count = ad->join_count();
    buyer_reporting_dispatch_request_data_.recency = ad->recency();
    buyer_reporting_dispatch_request_data_.modeling_signals =
        ad->modeling_signals();
    buyer_reporting_dispatch_request_data_.ad_cost = ad->ad_cost();
    buyer_reporting_dispatch_request_data_.winning_ad_render_url = ad->render();

    if (!ad->buyer_reporting_id().empty()) {
      // Set buyer_reporting_id in the response.
      // If this id doesn't match with the buyerReportingId on-device, reportWin
      // urls will be dropped.
      raw_response_.mutable_ad_score()->set_buyer_reporting_id(
          ad->buyer_reporting_id());
      buyer_reporting_dispatch_request_data_.buyer_reporting_id =
          ad->buyer_reporting_id();
    }
    if (auction_scope_ ==
            AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
        auction_scope_ ==
            AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
      DispatchReportResultRequestForComponentAuction(winning_ad_score);
    } else {
      DispatchReportResultRequest(winning_ad_score);
    }
  } else if (auto protected_app_signals_ad_it =
                 protected_app_signals_ad_data_.find(id);
             protected_app_signals_ad_it !=
             protected_app_signals_ad_data_.end()) {
    post_auction_signals_ = GeneratePostAuctionSignals(
        winning_ad_score, raw_request_.seller_currency());
    InitializeBuyerReportingDispatchRequestData(winning_ad_score);

    const auto& protected_app_signals_ad = protected_app_signals_ad_it->second;
    buyer_reporting_dispatch_request_data_.modeling_signals =
        protected_app_signals_ad->modeling_signals();
    buyer_reporting_dispatch_request_data_.ad_cost =
        protected_app_signals_ad->ad_cost();
    buyer_reporting_dispatch_request_data_.winning_ad_render_url =
        protected_app_signals_ad->render();
    buyer_reporting_dispatch_request_data_.egress_payload =
        protected_app_signals_ad->egress_payload();
    buyer_reporting_dispatch_request_data_.temporary_unlimited_egress_payload =
        protected_app_signals_ad->temporary_unlimited_egress_payload();
    if (auction_scope_ ==
            AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
        auction_scope_ ==
            AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
      PS_LOG(ERROR, log_context_)
          << "Multi seller auction is unavailable for PAS";
    } else {
      DispatchReportResultRequest(winning_ad_score);
    }
  } else {
    PS_LOG(ERROR, log_context_)
        << "Following id didn't map to any ProtectedAudience or "
           "ProtectedAppSignals Ad: "
        << id;
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return;
  }
}

void ScoreAdsReactor::PerformReporting(
    const ScoreAdsResponse::AdScore& winning_ad_score, absl::string_view id) {
  if (auto ad_it = ad_data_.find(id); ad_it != ad_data_.end()) {
    const auto& ad = ad_it->second;
    BuyerReportingMetadata buyer_reporting_metadata;
    if (enable_report_win_url_generation_) {
      buyer_reporting_metadata = {
          .buyer_signals = raw_request_.per_buyer_signals().at(
              winning_ad_score.interest_group_owner()),
          .join_count = ad->join_count(),
          .recency = ad->recency(),
          .modeling_signals = ad->modeling_signals(),
          .seller = raw_request_.seller(),
          .interest_group_name = winning_ad_score.interest_group_name(),
          .ad_cost = ad->ad_cost()};
      if (!ad->buyer_reporting_id().empty()) {
        raw_response_.mutable_ad_score()->set_buyer_reporting_id(
            ad->buyer_reporting_id());
        buyer_reporting_metadata.buyer_reporting_id = ad->buyer_reporting_id();
      }
    }
    DispatchReportingRequestForPA(id, winning_ad_score,
                                  BuildAuctionConfig(raw_request_),
                                  buyer_reporting_metadata);

  } else if (auto protected_app_signals_ad_it =
                 protected_app_signals_ad_data_.find(id);
             protected_app_signals_ad_it !=
             protected_app_signals_ad_data_.end()) {
    const auto& ad = protected_app_signals_ad_it->second;
    BuyerReportingMetadata buyer_reporting_metadata;
    if (enable_report_win_url_generation_) {
      buyer_reporting_metadata = {
          .buyer_signals = raw_request_.per_buyer_signals().at(
              winning_ad_score.interest_group_owner()),
          .modeling_signals = ad->modeling_signals(),
          .seller = raw_request_.seller(),
          .interest_group_name = winning_ad_score.interest_group_name(),
          .ad_cost = ad->ad_cost()};
    }
    DispatchReportingRequestForPAS(
        winning_ad_score, BuildAuctionConfig(raw_request_),
        buyer_reporting_metadata, ad->egress_payload(),
        ad->temporary_unlimited_egress_payload());

  } else {
    PS_LOG(ERROR, log_context_)
        << "Following id didn't map to any ProtectedAudience or "
           "ProtectedAppSignals Ad: "
        << id;
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return;
  }
}

void ScoreAdsReactor::HandleScoredAd(int index, float buyer_bid,
                                     absl::string_view ad_with_bid_currency,
                                     absl::string_view interest_group_name,
                                     absl::string_view interest_group_owner,
                                     absl::string_view interest_group_origin,
                                     const rapidjson::Document& response_json,
                                     AdType ad_type,
                                     ScoreAdsResponse::AdScore& ad_score,
                                     ScoringData& scoring_data,
                                     const std::string& dispatch_response_id) {
  // Get ad rejection reason before updating the scoring data.
  std::optional<ScoreAdsResponse::AdScore::AdRejectionReason>
      ad_rejection_reason;
  if (!response_json.IsNumber()) {
    // Parse Ad rejection reason and store only if it has value.
    ad_rejection_reason = ParseAdRejectionReason(
        response_json, interest_group_owner, interest_group_name, log_context_);
  }

  ad_score.set_interest_group_name(interest_group_name);
  ad_score.set_interest_group_owner(interest_group_owner);
  ad_score.set_interest_group_origin(interest_group_origin);
  ad_score.set_ad_type(ad_type);
  ad_score.set_buyer_bid(buyer_bid);
  ad_score.set_buyer_bid_currency(ad_with_bid_currency);
  // Used for debug reporting.
  // Should include bids rejected by bid currency mismatch
  // and those not allowed in component auctions.
  ad_scores_.emplace(dispatch_response_id,
                     std::make_unique<ScoreAdsResponse::AdScore>(ad_score));

  if (CheckAndUpdateModifiedBid(auction_scope_, buyer_bid, ad_with_bid_currency,
                                &ad_score)) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Setting modified bid value to original buyer bid value (and "
           "currency) as the "
           "modified bid is not set. For interest group: "
        << interest_group_name << ": " << ad_score.DebugString();
  }

  if (IsBidCurrencyMismatched(auction_scope_, raw_request_.seller_currency(),
                              ad_score.bid(), ad_score.bid_currency())) {
    ad_rejection_reason = ScoreAdsResponse::AdScore::AdRejectionReason{};
    ad_rejection_reason->set_interest_group_name(
        ad_score.interest_group_name());
    ad_rejection_reason->set_interest_group_owner(
        ad_score.interest_group_owner());
    ad_rejection_reason->set_rejection_reason(
        SellerRejectionReason::BID_FROM_SCORE_AD_FAILED_CURRENCY_CHECK);
    PS_VLOG(kNoisyInfo, log_context_)
        << "Skipping component bid as it does not match seller_currency: "
        << interest_group_name << ": " << ad_score.DebugString();
  }

  if (IsIncomingBidInSellerCurrencyIllegallyModified(
          raw_request_.seller_currency(), ad_with_bid_currency,
          ad_score.incoming_bid_in_seller_currency(), buyer_bid)) {
    ad_rejection_reason = ScoreAdsResponse::AdScore::AdRejectionReason{};
    ad_rejection_reason->set_interest_group_name(
        ad_score.interest_group_name());
    ad_rejection_reason->set_interest_group_owner(
        ad_score.interest_group_owner());
    ad_rejection_reason->set_rejection_reason(
        SellerRejectionReason::INVALID_BID);
    PS_VLOG(kNoisyInfo, log_context_)
        << "Skipping ad_score as its incomingBidInSellerCurrency was "
           "modified "
           "when it should not have been: "
        << interest_group_name << ": " << ad_score.DebugString();
  }

  const bool is_valid_ad =
      !ad_rejection_reason.has_value() ||
      ad_rejection_reason->rejection_reason() ==
          SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE;
  // Consider only ads that are not explicitly rejected and the ones that have
  // a positive desirability score.
  if (is_valid_ad && ad_score.desirability() >
                         scoring_data.desirability_of_most_desirable_ad) {
    if (auction_scope_ ==
            AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER &&
        !ad_score.allow_component_auction()) {
      // Ignore component level winner if it is not allowed to
      // participate in the top level auction.
      // TODO(b/311234165): Add metric for rejected component ads.
      PS_VLOG(kNoisyInfo, log_context_)
          << "Skipping component bid as it is not allowed for "
          << interest_group_name << ": " << ad_score.DebugString();
      return;
    }
    scoring_data.UpdateWinner(index, ad_score);
  }

  if (is_valid_ad && ad_score.desirability() > 0) {
    // Consider scored ad as valid (i.e. not rejected) when it has a
    // positive desirability and either:
    // 1. scoreAd returned a number.
    // 2. scoreAd returned an object but the reject reason was not
    // populated.
    // 3. scoreAd returned an object and the reject reason was explicitly
    // set to "not-available".
    // Only consider valid bids for populating other highest bids.
    scoring_data.score_ad_map[ad_score.desirability()].push_back(index);
    return;
  }

  // Populate a default rejection reason if needed when we didn't get a positive
  // desirability.
  if (ad_score.desirability() <= 0 && !ad_rejection_reason.has_value()) {
    PS_VLOG(5, log_context_)
        << "Non-positive desirability for ad and no rejection reason "
           "populated "
           "by seller, providing a default rejection reason";
    ad_rejection_reason = ScoreAdsResponse::AdScore::AdRejectionReason{};
    ad_rejection_reason->set_interest_group_owner(interest_group_owner);
    ad_rejection_reason->set_interest_group_name(interest_group_name);
    ad_rejection_reason->set_rejection_reason(
        SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
  }
  scoring_data.ad_rejection_reasons.push_back(*ad_rejection_reason);
  scoring_data.seller_rejected_bid_count += 1;
  LogIfError(
      metric_context_->AccumulateMetric<metric::kAuctionBidRejectedCount>(
          1, ToSellerRejectionReasonString(
                 ad_rejection_reason->rejection_reason())));
}

void ScoreAdsReactor::FindScoredAdType(
    absl::string_view response_id, AdWithBidMetadata** ad_with_bid_metadata,
    ProtectedAppSignalsAdWithBidMetadata**
        protected_app_signals_ad_with_bid_metadata) {
  if (auto ad_it = ad_data_.find(response_id); ad_it != ad_data_.end()) {
    *ad_with_bid_metadata = ad_it->second.get();
  } else if (auto protected_app_signals_ad_it =
                 protected_app_signals_ad_data_.find(response_id);
             protected_app_signals_ad_it !=
             protected_app_signals_ad_data_.end()) {
    *protected_app_signals_ad_with_bid_metadata =
        protected_app_signals_ad_it->second.get();
  }
}

ScoringData ScoreAdsReactor::FindWinningAd(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  ScoringData scoring_data;
  int64_t current_all_debug_urls_chars = 0;
  for (int index = 0; index < responses.size(); ++index) {
    const auto& response = responses[index];
    if (!response.ok()) {
      PS_LOG(WARNING, log_context_)
          << "Invalid execution (possibly invalid input): "
          << responses[index].status().ToString(
                 absl::StatusToStringMode::kWithEverything);
      LogIfError(
          metric_context_->LogUpDownCounter<metric::kJSExecutionErrorCount>(1));
      continue;
    }

    absl::StatusOr<rapidjson::Document> response_json =
        ParseAndGetScoreAdResponseJson(enable_adtech_code_logging_,
                                       response->resp, log_context_);

    // Determine what type of ad was scored in this response.
    AdWithBidMetadata* ad = nullptr;
    ProtectedAppSignalsAdWithBidMetadata* protected_app_signals_ad_with_bid =
        nullptr;
    FindScoredAdType(response->id, &ad, &protected_app_signals_ad_with_bid);
    if (!ad && !protected_app_signals_ad_with_bid) {
      // This should never happen but we log here in case there is a bug in
      // our implementation.
      PS_LOG(ERROR, log_context_)
          << "Scored ad is neither a protected audience ad, nor a "
             "protected app "
             "signals ad: "
          << response->resp;
      continue;
    }

    if (!response_json.ok()) {
      LogWarningForBadResponse(response_json.status(), *response, ad,
                               log_context_);
      continue;
    }

    auto ad_score = ParseScoreAdResponse(
        *response_json, max_allowed_size_debug_url_chars_,
        max_allowed_size_all_debug_urls_chars_,
        (auction_scope_ ==
             AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
         auction_scope_ ==
             AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER),
        current_all_debug_urls_chars);
    if (!ad_score.ok()) {
      LogWarningForBadResponse(ad_score.status(), *response, ad, log_context_);
      continue;
    }
    current_all_debug_urls_chars += DebugReportUrlsLength(*ad_score);

    if (ad) {
      HandleScoredAd(index, ad->bid(), ad->bid_currency(),
                     ad->interest_group_name(), ad->interest_group_owner(),
                     ad->interest_group_origin(), *response_json,
                     AdType::AD_TYPE_PROTECTED_AUDIENCE_AD, *ad_score,
                     scoring_data, response->id);
    } else {
      HandleScoredAd(index, protected_app_signals_ad_with_bid->bid(),
                     protected_app_signals_ad_with_bid->bid_currency(),
                     /*interest_group_name=*/"",
                     protected_app_signals_ad_with_bid->owner(),
                     /*interest_group_origin=*/"", *response_json,
                     AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD, *ad_score,
                     scoring_data, response->id);
    }
  }
  return scoring_data;
}

void ScoreAdsReactor::PopulateRelevantFieldsInResponse(
    int index_of_most_desirable_ad, absl::string_view request_id,
    ScoreAdsResponse::AdScore& winning_ad) {
  AdWithBidMetadata* ad = nullptr;
  ProtectedAppSignalsAdWithBidMetadata* protected_app_signals_ad_with_bid =
      nullptr;
  FindScoredAdType(request_id, &ad, &protected_app_signals_ad_with_bid);
  // Note: Before the call flow gets here, we would have already verified the
  // winning ad type is one of the expected types and hence we only do a DCHECK
  // here.
  DCHECK(ad || protected_app_signals_ad_with_bid);

  if (ad) {
    winning_ad.set_render(ad->render());
    winning_ad.mutable_component_renders()->Swap(ad->mutable_ad_components());
  } else {
    winning_ad.set_render(protected_app_signals_ad_with_bid->render());
  }
}

void ScoreAdsReactor::PopulateHighestScoringOtherBidsData(
    int index_of_most_desirable_ad_score,
    const absl::flat_hash_map<float, std::list<int>>& score_ad_map,
    const std::vector<absl::StatusOr<DispatchResponse>>& responses,
    ScoreAdsResponse::AdScore& winning_ad_score) {
  if (auction_scope_ == AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    return;
  }
  std::vector<float> scores_list;
  // Logic to calculate the list of highest scoring other bids and
  // corresponding IG owners.
  for (const auto& [score, unused_ad_indices] : score_ad_map) {
    scores_list.push_back(score);
  }

  // Sort the scores in descending order.
  std::sort(scores_list.begin(), scores_list.end(),
            [](int a, int b) { return a > b; });

  // Add all the bids with the top 2 scores (excluding the winner and bids
  // with 0 score) and corresponding interest group owners to
  // ig_owner_highest_scoring_other_bids_map.
  for (int i = 0; i < 2 && i < scores_list.size(); i++) {
    if (scores_list.at(i) == 0) {
      break;
    }

    for (int current_index : score_ad_map.at(scores_list.at(i))) {
      if (index_of_most_desirable_ad_score == current_index) {
        continue;
      }

      AdWithBidMetadata* ad_with_bid_metadata_from_buyer = nullptr;
      ProtectedAppSignalsAdWithBidMetadata* protected_app_signals_ad_with_bid =
          nullptr;
      FindScoredAdType(responses[current_index]->id,
                       &ad_with_bid_metadata_from_buyer,
                       &protected_app_signals_ad_with_bid);
      DCHECK(ad_with_bid_metadata_from_buyer ||
             protected_app_signals_ad_with_bid);
      auto* highest_scoring_other_bids_map =
          winning_ad_score.mutable_ig_owner_highest_scoring_other_bids_map();

      std::string owner;
      float bid = 0.0;
      if (ad_with_bid_metadata_from_buyer != nullptr) {
        bid = ad_with_bid_metadata_from_buyer->bid();
        owner = ad_with_bid_metadata_from_buyer->interest_group_owner();
      } else {
        bid = protected_app_signals_ad_with_bid->bid();
        owner = protected_app_signals_ad_with_bid->owner();
      }
      if (!raw_request_.seller_currency().empty()) {
        auto ad_score_it = ad_scores_.find(responses[current_index]->id);
        if (ad_score_it != ad_scores_.end()) {
          bid = ad_score_it->second->incoming_bid_in_seller_currency();
        }
      }
      UpdateHighestScoringOtherBidMap(bid, owner,
                                      *highest_scoring_other_bids_map);
    }
  }
}

// Handles the output of the code execution dispatch.
// Note that the dispatch response value is expected to be a json string
// conforming to the scoreAd function output described here:
// https://github.com/WICG/turtledove/blob/main/FLEDGE.md#23-scoring-bids
void ScoreAdsReactor::ScoreAdsCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses,
    bool enable_debug_reporting) {
  MayVlogRomaResponses(responses, log_context_);
  benchmarking_logger_->HandleResponseBegin();
  int total_bid_count = static_cast<int>(responses.size());
  LogIfError(metric_context_->AccumulateMetric<metric::kAuctionTotalBidsCount>(
      total_bid_count));
  ScoringData scoring_data = FindWinningAd(responses);
  LogIfError(metric_context_->LogHistogram<metric::kAuctionBidRejectedPercent>(
      (static_cast<double>(scoring_data.seller_rejected_bid_count)) /
      total_bid_count));
  auto& winning_ad = scoring_data.winning_ad;
  // No Ad won.
  if (!winning_ad.has_value()) {
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                       1, metric::kAuctionScoreAdsNoAdSelected));
    PS_LOG(WARNING, log_context_) << "No ad was selected as most desirable";
    if (enable_debug_reporting) {
      PerformDebugReporting(winning_ad);
    }
    EncryptAndFinishOK();
    return;
  }

  // Set the render URL in overall response for the winning ad.
  const int index_of_most_desirable_ad =
      scoring_data.index_of_most_desirable_ad;
  const auto& id = responses[index_of_most_desirable_ad]->id;
  PopulateRelevantFieldsInResponse(index_of_most_desirable_ad, id, *winning_ad);

  PopulateHighestScoringOtherBidsData(index_of_most_desirable_ad,
                                      scoring_data.score_ad_map, responses,
                                      *winning_ad);

  const auto& ad_rejection_reasons = scoring_data.ad_rejection_reasons;
  winning_ad->mutable_ad_rejection_reasons()->Assign(
      ad_rejection_reasons.begin(), ad_rejection_reasons.end());

  if (enable_debug_reporting) {
    PerformDebugReporting(winning_ad);
  }
  *raw_response_.mutable_ad_score() = *winning_ad;
  if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    PopulateComponentReportingUrlsInTopLevelResponse(
        id, raw_response_, component_level_reporting_data_);
  }
  // Todo(b/357146634): Execute reportWin even if
  // enable_report_result_url_generation is disabled or reportResult execution
  // fails.
  if (enable_report_result_url_generation_) {
    if (enable_seller_and_buyer_code_isolation_) {
      PerformReportingWithSellerAndBuyerCodeIsolation(*winning_ad, id);
      return;
    }
    PerformReporting(*winning_ad, id);
    return;
  }
  EncryptAndFinishOK();
  return;
}

void ScoreAdsReactor::ReportingCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  if (PS_VLOG_IS_ON(2)) {
    for (const auto& dispatch_response : responses) {
      PS_VLOG(kDispatch, log_context_)
          << "Reporting V8 Response: " << dispatch_response.status();
      if (dispatch_response.ok()) {
        PS_VLOG(kDispatch, log_context_) << dispatch_response.value().resp;
      }
    }
  }
  for (const auto& response : responses) {
    if (response.ok()) {
      absl::StatusOr<ReportingResponse> reporting_response =
          ParseAndGetReportingResponse(enable_adtech_code_logging_,
                                       response.value().resp);
      if (!reporting_response.ok()) {
        PS_LOG(ERROR, log_context_) << "Failed to parse response from Roma ",
            reporting_response.status().ToString(
                absl::StatusToStringMode::kWithEverything);
        continue;
      }

      auto LogUdf = [this](const std::vector<std::string>& adtech_logs,
                           absl::string_view prefix) {
        if (!enable_adtech_code_logging_) {
          return;
        }
        if (!AllowAnyUdfLogging(log_context_)) {
          return;
        }
        for (const std::string& log : adtech_logs) {
          std::string log_str =
              absl::StrCat(prefix, " execution script: ", log);
          PS_VLOG(kUdfLog, log_context_) << log_str;
          log_context_.SetEventMessageField(log_str);
        }
      };
      LogUdf(reporting_response->seller_logs, "Log from Seller's");
      LogUdf(reporting_response->seller_error_logs, "Error Log from Seller's");
      LogUdf(reporting_response->seller_warning_logs,
             "Warning Log from Seller's");
      LogUdf(reporting_response->buyer_logs, "Log from Buyer's");
      LogUdf(reporting_response->buyer_error_logs, "Error Log from Buyer's");
      LogUdf(reporting_response->buyer_warning_logs,
             "Warning Log from Buyer's");

      // For component auctions, the reporting urls for seller are set in
      // the component_seller_reporting_urls field. For single seller
      // auctions and top level auctions, the reporting urls for the top level
      // seller are set in the top_level_seller_reporting_urls field. For top
      // level auctions, the component_seller_reporting_urls and
      // buyer_reporting_urls are set based on the urls obtained from the
      // component auction whose buyer won the top level auction.
      if (auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER) {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_component_seller_reporting_urls()
            ->set_reporting_url(
                reporting_response->report_result_response.report_result_url);
        for (const auto& [event, interactionReportingUrl] :
             reporting_response->report_result_response
                 .interaction_reporting_urls) {
          raw_response_.mutable_ad_score()
              ->mutable_win_reporting_urls()
              ->mutable_component_seller_reporting_urls()
              ->mutable_interaction_reporting_urls()
              ->try_emplace(event, interactionReportingUrl);
        }

      } else if (auction_scope_ ==
                 AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_top_level_seller_reporting_urls()
            ->set_reporting_url(
                reporting_response->report_result_response.report_result_url);
        for (const auto& [event, interactionReportingUrl] :
             reporting_response->report_result_response
                 .interaction_reporting_urls) {
          raw_response_.mutable_ad_score()
              ->mutable_win_reporting_urls()
              ->mutable_top_level_seller_reporting_urls()
              ->mutable_interaction_reporting_urls()
              ->try_emplace(event, interactionReportingUrl);
        }
      } else {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_top_level_seller_reporting_urls()
            ->set_reporting_url(
                reporting_response->report_result_response.report_result_url);
        for (const auto& [event, interactionReportingUrl] :
             reporting_response->report_result_response
                 .interaction_reporting_urls) {
          raw_response_.mutable_ad_score()
              ->mutable_win_reporting_urls()
              ->mutable_top_level_seller_reporting_urls()
              ->mutable_interaction_reporting_urls()
              ->try_emplace(event, interactionReportingUrl);
        }
      }
      if (enable_report_win_url_generation_) {
        raw_response_.mutable_ad_score()
            ->mutable_win_reporting_urls()
            ->mutable_buyer_reporting_urls()
            ->set_reporting_url(
                reporting_response->report_win_response.report_win_url);

        for (const auto& [event, interactionReportingUrl] :
             reporting_response->report_win_response
                 .interaction_reporting_urls) {
          raw_response_.mutable_ad_score()
              ->mutable_win_reporting_urls()
              ->mutable_buyer_reporting_urls()
              ->mutable_interaction_reporting_urls()
              ->try_emplace(event, interactionReportingUrl);
        }
      }
    } else {
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                         1, metric::kAuctionScoreAdsDispatchResponseError));
      PS_LOG(WARNING, log_context_)
          << "Invalid execution (possibly invalid input): "
          << response.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    }
  }
  EncryptAndFinishOK();
}

void ScoreAdsReactor::ReportWinCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  for (const auto& response : responses) {
    if (!response.ok()) {
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                         1, metric::kAuctionScoreAdsDispatchResponseError));
      PS_VLOG(kDispatch, log_context_)
          << "Error executing reportWin udf:"
          << response.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
      continue;
    }
    PS_VLOG(kDispatch, log_context_)
        << "Successfully executed reportWin udf. Response:"
        << response.value().resp;
    absl::StatusOr<ReportWinResponse> report_win_response =
        ParseReportWinResponse(reporting_dispatch_request_config_,
                               response.value().resp, log_context_);
    if (!report_win_response.ok()) {
      PS_LOG(ERROR, log_context_)
          << "Failed to parse reportWin response from Roma ",
          report_win_response.status().ToString(
              absl::StatusToStringMode::kWithEverything);
      continue;
    }
    raw_response_.mutable_ad_score()
        ->mutable_win_reporting_urls()
        ->mutable_buyer_reporting_urls()
        ->set_reporting_url(report_win_response->report_win_url);
    for (const auto& [event, interactionReportingUrl] :
         report_win_response->interaction_reporting_urls) {
      raw_response_.mutable_ad_score()
          ->mutable_win_reporting_urls()
          ->mutable_buyer_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, interactionReportingUrl);
    }
  }
  EncryptAndFinishOK();
}

void ScoreAdsReactor::CancellableReportResultCallback(
    const std::vector<absl::StatusOr<DispatchResponse>>& responses) {
  for (const auto& response : responses) {
    if (!response.ok()) {
      LogIfError(metric_context_
                     ->AccumulateMetric<metric::kAuctionErrorCountByErrorCode>(
                         1, metric::kAuctionScoreAdsDispatchResponseError));
      PS_VLOG(kDispatch, log_context_)
          << "Error executing reportResult udf:"
          << response.status().ToString(
                 absl::StatusToStringMode::kWithEverything);
    } else {  // Parse ReportResultResponse and set it in scoreAd response.
      absl::StatusOr<ReportResultResponse> report_result_response =
          ParseReportResultResponse(reporting_dispatch_request_config_,
                                    response.value().resp, log_context_);
      if (!report_result_response.ok()) {
        PS_LOG(ERROR, log_context_)
            << "Failed to parse report result response from Roma ",
            report_result_response.status().ToString(
                absl::StatusToStringMode::kWithEverything);
      } else {
        buyer_reporting_dispatch_request_data_.signals_for_winner =
            report_result_response->signals_for_winner;
        if (auction_scope_ ==
                AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
            auction_scope_ ==
                AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
          SetSellerReportingUrlsInResponseForComponentAuction(
              std::move(*report_result_response), raw_response_);
        } else {
          SetSellerReportingUrlsInResponse(std::move(*report_result_response),
                                           raw_response_);
        }
      }
    }
    // Execute reportWin()
    if (!enable_report_win_url_generation_) {
      EncryptAndFinishOK();
      return;
    }
    absl::Status report_win_status;
    if (!buyer_reporting_dispatch_request_data_.egress_payload.has_value()) {
      report_win_status = PerformPAReportWin(
          reporting_dispatch_request_config_,
          buyer_reporting_dispatch_request_data_, seller_device_signals_,
          [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            ReportWinCallback(result);
          },
          dispatcher_);
    } else {
      report_win_status = PerformPASReportWin(
          reporting_dispatch_request_config_,
          buyer_reporting_dispatch_request_data_, seller_device_signals_,
          [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            ReportWinCallback(result);
          },
          dispatcher_);
    }
    if (!report_win_status.ok()) {
      PS_VLOG(kDispatch, log_context_)
          << "Execution failed for reportWin: " << report_win_status.message();
      EncryptAndFinishOK();
      return;
    }
    return;
  }
  EncryptAndFinishOK();
}

void ScoreAdsReactor::PerformDebugReporting(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score) {
  if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    return;
  }
  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      winning_ad_score, raw_request_.seller_currency());
  for (const auto& [id, ad_score] : ad_scores_) {
    if (ad_score->has_debug_report_urls()) {
      absl::string_view debug_url;
      bool is_win_debug_url = false;
      const auto& ig_owner = ad_score->interest_group_owner();
      const auto& ig_name = ad_score->interest_group_name();
      if (ig_owner == post_auction_signals.winning_ig_owner &&
          ig_name == post_auction_signals.winning_ig_name) {
        debug_url = ad_score->debug_report_urls().auction_debug_win_url();
        is_win_debug_url = true;
      } else {
        debug_url = ad_score->debug_report_urls().auction_debug_loss_url();
      }
      if (debug_url.empty()) {
        continue;
      }

      absl::AnyInvocable<void(absl::StatusOr<absl::string_view>)> done_callback;
      if (PS_VLOG_IS_ON(5)) {
        done_callback = [ig_owner, ig_name](
                            // NOLINTNEXTLINE
                            absl::StatusOr<absl::string_view> result) mutable {
          if (result.ok()) {
            PS_VLOG(5) << "Performed debug reporting for:" << ig_owner
                       << ", interest_group: " << ig_name;
          } else {
            PS_VLOG(5) << "Error while performing debug reporting for:"
                       << ig_owner << ", interest_group: " << ig_name
                       << " ,status:" << result.status();
          }
        };
      } else {
        // NOLINTNEXTLINE
        done_callback = [](absl::StatusOr<absl::string_view> result) {};
      }
      const HTTPRequest http_request = CreateDebugReportingHttpRequest(
          debug_url,
          GetPlaceholderDataForInterestGroup(ig_owner, ig_name,
                                             post_auction_signals),
          is_win_debug_url);
      async_reporter_.DoReport(http_request, std::move(done_callback));
    }
  }
}

void ScoreAdsReactor::EncryptAndFinishOK() {
  PS_VLOG(kPlain, log_context_)
      << "ScoreAdsRawResponse exported in EventMessage";
  log_context_.SetEventMessageField(raw_response_);
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(/*if_export_consented=*/true);
  EncryptResponse();
  PS_VLOG(kEncrypted, log_context_) << "Encrypted ScoreAdsResponse\n"
                                    << response_->ShortDebugString();
  benchmarking_logger_->HandleResponseEnd();
  FinishWithStatus(grpc::Status::OK);
}

void ScoreAdsReactor::FinishWithStatus(const grpc::Status& status) {
  if (status.error_code() != grpc::StatusCode::OK) {
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  }
  Finish(status);
}

void ScoreAdsReactor::DispatchReportingRequestForPA(
    absl::string_view dispatch_id,
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::shared_ptr<std::string>& auction_config,
    const BuyerReportingMetadata& buyer_reporting_metadata) {
  PostAuctionSignals post_auction_signals;
  if (auction_scope_ == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    post_auction_signals =
        GeneratePostAuctionSignalsForTopLevelSeller(winning_ad_score);
  } else {
    post_auction_signals = GeneratePostAuctionSignals(
        winning_ad_score, raw_request_.seller_currency());
  }
  ReportingDispatchRequestData dispatch_request_data = {
      .handler_name = kReportingDispatchHandlerFunctionName,
      .auction_config = auction_config,
      .post_auction_signals = post_auction_signals,
      .publisher_hostname = raw_request_.publisher_hostname(),
      .log_context = log_context_,
      .buyer_reporting_metadata = buyer_reporting_metadata};
  if (auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
      auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
    dispatch_request_data.component_reporting_metadata = {
        .top_level_seller = raw_request_.top_level_seller(),
        .component_seller = raw_request_.seller()};
  } else if (auction_scope_ == AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    if (auto ad_it = component_level_reporting_data_.find(dispatch_id);
        ad_it != component_level_reporting_data_.end()) {
      dispatch_request_data.component_reporting_metadata = {
          .component_seller = ad_it->second.component_seller};
    } else {
      PS_LOG(ERROR, log_context_) << "Could not find component reporting data "
                                     "for winner ad with dispatch id: "
                                  << dispatch_id;
    }
  }
  if (winning_ad_score.bid() > 0) {
    dispatch_request_data.component_reporting_metadata.modified_bid =
        winning_ad_score.bid();
  } else {
    dispatch_request_data.component_reporting_metadata.modified_bid =
        winning_ad_score.buyer_bid();
  }
  // By this point in the logic, the bid currency has already been set
  // to refer to the modified bid if present or the buyer_bid if not (by
  // CheckAndUpdateModifiedBid() in HandleScoredAd()), so no logic is needed
  // to find the right currency.
  dispatch_request_data.component_reporting_metadata.modified_bid_currency =
      winning_ad_score.bid_currency();
  DispatchReportingRequest(dispatch_request_data);
}

void ScoreAdsReactor::DispatchReportResultRequest(
    const ScoreAdsResponse::AdScore& winning_ad_score) {
  SellerReportingDispatchRequestData dispatch_request_data = {
      .auction_config = buyer_reporting_dispatch_request_data_.auction_config,
      .post_auction_signals = post_auction_signals_,
      .publisher_hostname = raw_request_.publisher_hostname(),
      .log_context = log_context_};
  seller_device_signals_ = GenerateSellerDeviceSignals(dispatch_request_data);
  absl::Status dispatched = PerformReportResult(
      reporting_dispatch_request_config_, seller_device_signals_,
      dispatch_request_data,
      [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        ReportResultCallback(result);
      },
      dispatcher_);
  if (!dispatched.ok()) {
    PS_VLOG(kDispatch, log_context_)
        << "Perform report result for failed" << dispatched.message();
    EncryptAndFinishOK();
    return;
  }
  if (PS_VLOG_IS_ON(kNoisyInfo)) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Performed report result for seller. Winning ad:"
        << post_auction_signals_.winning_ad_render_url;
  }
}

void ScoreAdsReactor::DispatchReportResultRequestForComponentAuction(
    const ScoreAdsResponse::AdScore& winning_ad_score) {
  SellerReportingDispatchRequestData dispatch_request_data = {
      .auction_config = buyer_reporting_dispatch_request_data_.auction_config,
      .post_auction_signals = post_auction_signals_,
      .publisher_hostname = raw_request_.publisher_hostname(),
      .log_context = log_context_};
  dispatch_request_data.component_reporting_metadata = {
      .top_level_seller = raw_request_.top_level_seller()};
  if (winning_ad_score.bid() > 0) {
    dispatch_request_data.component_reporting_metadata.modified_bid =
        winning_ad_score.bid();
  } else {
    dispatch_request_data.component_reporting_metadata.modified_bid =
        winning_ad_score.buyer_bid();
  }
  seller_device_signals_ =
      GenerateSellerDeviceSignalsForComponentAuction(dispatch_request_data);
  absl::Status dispatched = PerformReportResult(
      reporting_dispatch_request_config_, seller_device_signals_,
      dispatch_request_data,
      [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        ReportResultCallback(result);
      },
      dispatcher_);
  if (!dispatched.ok()) {
    PS_VLOG(kDispatch, log_context_)
        << "Perform report result for failed" << dispatched.message();
    EncryptAndFinishOK();
    return;
  }
  if (PS_VLOG_IS_ON(kNoisyInfo)) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Perform report result for seller. Winning ad:"
        << post_auction_signals_.winning_ad_render_url;
  }
}

void ScoreAdsReactor::DispatchReportResultRequestForTopLevelAuction(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    absl::string_view dispatch_id) {
  SellerReportingDispatchRequestData dispatch_request_data = {
      .auction_config = BuildAuctionConfig(raw_request_),
      .post_auction_signals = post_auction_signals_,
      .publisher_hostname = raw_request_.publisher_hostname(),
      .log_context = log_context_};
  if (auto ad_it = component_level_reporting_data_.find(dispatch_id);
      ad_it != component_level_reporting_data_.end()) {
    dispatch_request_data.component_reporting_metadata = {
        .component_seller = ad_it->second.component_seller};
    // Set buyer_reporting_id in the response.
    // If this id doesn't match with the buyerReportingId on-device, reportWin
    // urls will be dropped.
    raw_response_.mutable_ad_score()->set_buyer_reporting_id(
        ad_it->second.buyer_reporting_id);
  } else {
    PS_VLOG(kDispatch, log_context_)
        << "Could not find component reporting data "
           "for winner ad with dispatch id: "
        << dispatch_id;
  }
  seller_device_signals_ =
      GenerateSellerDeviceSignalsForTopLevelAuction(dispatch_request_data);
  absl::Status dispatched = PerformReportResult(
      reporting_dispatch_request_config_, seller_device_signals_,
      dispatch_request_data,
      [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
        ReportResultCallback(result);
      },
      dispatcher_);
  if (!dispatched.ok()) {
    PS_VLOG(kDispatch, log_context_)
        << "Perform report result for failed" << dispatched.message();
    EncryptAndFinishOK();
    return;
  }
  if (PS_VLOG_IS_ON(kNoisyInfo)) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "Performed report result for seller. Winning ad:"
        << post_auction_signals_.winning_ad_render_url;
  }
}

void ScoreAdsReactor::DispatchReportingRequestForPAS(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::shared_ptr<std::string>& auction_config,
    const BuyerReportingMetadata& buyer_reporting_metadata,
    std::string_view egress_payload,
    absl::string_view temporary_egress_payload) {
  DispatchReportingRequest(
      {.handler_name = kReportingProtectedAppSignalsFunctionName,
       .auction_config = auction_config,
       .post_auction_signals = GeneratePostAuctionSignals(
           winning_ad_score, raw_request_.seller_currency()),
       .publisher_hostname = raw_request_.publisher_hostname(),
       .log_context = log_context_,
       .buyer_reporting_metadata = buyer_reporting_metadata,
       .egress_payload = egress_payload,
       .temporary_egress_payload = temporary_egress_payload});
}

void ScoreAdsReactor::DispatchReportingRequest(
    const ReportingDispatchRequestData& dispatch_request_data) {
  if (enable_cancellation_ && context_->IsCancelled()) {
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  ReportingDispatchRequestConfig dispatch_request_config = {
      .enable_report_win_url_generation = enable_report_win_url_generation_,
      .enable_protected_app_signals = enable_protected_app_signals_,
      .enable_report_win_input_noising = enable_report_win_input_noising_,
      .enable_adtech_code_logging = enable_adtech_code_logging_};
  DispatchRequest dispatch_request = GetReportingDispatchRequest(
      dispatch_request_config, dispatch_request_data);
  dispatch_request.tags[kRomaTimeoutMs] = roma_timeout_ms_;

  std::vector<DispatchRequest> dispatch_requests = {dispatch_request};
  auto status = dispatcher_.BatchExecute(
      dispatch_requests,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this](const std::vector<absl::StatusOr<DispatchResponse>>& result) {
            ReportingCallback(result);
          },
          [this]() {
            FinishWithStatus(
                grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
          }));

  if (!status.ok()) {
    std::string original_request;
    google::protobuf::TextFormat::PrintToString(raw_request_,
                                                &original_request);
    PS_LOG(ERROR, log_context_)
        << "Reporting execution request failed for batch: " << original_request
        << status.ToString(absl::StatusToStringMode::kWithEverything);
    EncryptAndFinishOK();
  }
}
}  // namespace privacy_sandbox::bidding_auction_servers
