//  Copyright 2024 Google LLC
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

#include "services/bidding_service/generate_bids_binary_reactor.h"

#include <utility>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/error_categories.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using IGForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;

inline constexpr absl::Duration kDefaultGenerateBidExecutionTimeout =
    absl::Seconds(1);

roma_service::GenerateProtectedAudienceBidRequest
BuildProtectedAudienceBidRequest(RawRequest& raw_request, bool logging_enabled,
                                 bool debug_reporting_enabled) {
  roma_service::GenerateProtectedAudienceBidRequest bid_request;
  bid_request.set_auction_signals(
      std::move(*raw_request.mutable_auction_signals()));
  bid_request.set_per_buyer_signals(
      std::move(*raw_request.mutable_buyer_signals()));
  // Populate server metadata.
  roma_service::ServerMetadata* server_metadata =
      bid_request.mutable_server_metadata();
  server_metadata->set_logging_enabled(logging_enabled);
  server_metadata->set_debug_reporting_enabled(debug_reporting_enabled);
  return bid_request;
}

void UpdateProtectedAudienceBidRequest(
    roma_service::GenerateProtectedAudienceBidRequest& bid_request,
    const RawRequest& raw_request, IGForBidding& ig_for_bidding) {
  // Populate interest group.
  roma_service::ProtectedAudienceInterestGroup* interest_group =
      bid_request.mutable_interest_group();
  *interest_group->mutable_name() = std::move(*ig_for_bidding.mutable_name());
  *interest_group->mutable_trusted_bidding_signals_keys() = {
      std::make_move_iterator(
          ig_for_bidding.mutable_trusted_bidding_signals_keys()->begin()),
      std::make_move_iterator(
          ig_for_bidding.mutable_trusted_bidding_signals_keys()->end())};
  *interest_group->mutable_ad_render_ids() = {
      std::make_move_iterator(ig_for_bidding.mutable_ad_render_ids()->begin()),
      std::make_move_iterator(ig_for_bidding.mutable_ad_render_ids()->end())};
  *interest_group->mutable_ad_component_render_ids() = {
      std::make_move_iterator(
          ig_for_bidding.mutable_ad_component_render_ids()->begin()),
      std::make_move_iterator(
          ig_for_bidding.mutable_ad_component_render_ids()->end())};
  *interest_group->mutable_user_bidding_signals() =
      std::move(*ig_for_bidding.mutable_user_bidding_signals());
  *bid_request.mutable_trusted_bidding_signals() =
      std::move(*ig_for_bidding.mutable_trusted_bidding_signals());

  // Populate (oneof) device signals.
  if (ig_for_bidding.has_android_signals_for_bidding() &&
      ig_for_bidding.android_signals_for_bidding().IsInitialized()) {
    roma_service::ProtectedAudienceAndroidSignals* android_signals =
        bid_request.mutable_android_signals();
    android_signals->set_top_level_seller(raw_request.top_level_seller());
  } else if (ig_for_bidding.has_browser_signals_for_bidding() &&
             ig_for_bidding.browser_signals_for_bidding().IsInitialized()) {
    roma_service::ProtectedAudienceBrowserSignals* browser_signals =
        bid_request.mutable_browser_signals();
    browser_signals->set_top_window_hostname(raw_request.publisher_name());
    browser_signals->set_seller(raw_request.seller());
    browser_signals->set_top_level_seller(raw_request.top_level_seller());
    browser_signals->set_join_count(
        ig_for_bidding.browser_signals_for_bidding().join_count());
    browser_signals->set_bid_count(
        ig_for_bidding.browser_signals_for_bidding().bid_count());
    if (ig_for_bidding.browser_signals_for_bidding().has_recency_ms()) {
      browser_signals->set_recency(
          ig_for_bidding.browser_signals_for_bidding().recency_ms());
    } else {
      browser_signals->set_recency(
          ig_for_bidding.browser_signals_for_bidding().recency() * 1000);
    }
    *browser_signals->mutable_prev_wins() =
        std::move(*ig_for_bidding.mutable_browser_signals_for_bidding()
                       ->mutable_prev_wins());
    *browser_signals->mutable_prev_wins_ms() =
        std::move(*ig_for_bidding.mutable_browser_signals_for_bidding()
                       ->mutable_prev_wins_ms());
    browser_signals->set_multi_bid_limit(raw_request.multi_bid_limit() > 0
                                             ? raw_request.multi_bid_limit()
                                             : kDefaultMultiBidLimit);
  }
}

std::vector<AdWithBid> ParseProtectedAudienceBids(
    google::protobuf::RepeatedPtrField<roma_service::ProtectedAudienceBid>&
        bids,
    absl::string_view ig_name, bool debug_reporting_enabled) {
  // Reserve memory to prevent reallocation.
  std::vector<AdWithBid> ads_with_bids;
  if (bids.size() == 0) {
    return ads_with_bids;
  }
  ads_with_bids.reserve(1);

  // TODO(b/371250031): Iterate through every roma_service::ProtectedAudienceBid
  // and build corresponding AdWithBid when K-anon is enabled.
  roma_service::ProtectedAudienceBid& bid = bids[0];
  AdWithBid ad_with_bid;
  *ad_with_bid.mutable_ad()->mutable_string_value() =
      std::move(*bid.mutable_ad());
  ad_with_bid.set_bid(bid.bid());
  *ad_with_bid.mutable_render() = std::move(*bid.mutable_render());
  *ad_with_bid.mutable_ad_components() = {
      std::make_move_iterator(bid.mutable_ad_components()->begin()),
      std::make_move_iterator(bid.mutable_ad_components()->end())};
  ad_with_bid.set_allow_component_auction(bid.allow_component_auction());
  ad_with_bid.set_interest_group_name(
      ig_name);  // Cannot move since it can be shared by multiple bids
  ad_with_bid.set_ad_cost(bid.ad_cost());
  if (bid.has_debug_report_urls() && debug_reporting_enabled) {
    *(ad_with_bid.mutable_debug_report_urls()
          ->mutable_auction_debug_win_url()) =
        std::move(
            *bid.mutable_debug_report_urls()->mutable_auction_debug_win_url());
    *(ad_with_bid.mutable_debug_report_urls()
          ->mutable_auction_debug_loss_url()) =
        std::move(
            *bid.mutable_debug_report_urls()->mutable_auction_debug_loss_url());
  }
  ad_with_bid.set_modeling_signals(bid.modeling_signals());
  *ad_with_bid.mutable_bid_currency() = std::move(*bid.mutable_bid_currency());
  if (!bid.mutable_buyer_reporting_id()->empty()) {
    *ad_with_bid.mutable_buyer_reporting_id() =
        std::move(*bid.mutable_buyer_reporting_id());
  }
  if (!bid.mutable_buyer_and_seller_reporting_id()->empty()) {
    *ad_with_bid.mutable_buyer_and_seller_reporting_id() =
        std::move(*bid.mutable_buyer_and_seller_reporting_id());
  }
  if (!bid.mutable_selected_buyer_and_seller_reporting_id()->empty()) {
    *ad_with_bid.mutable_selected_buyer_and_seller_reporting_id() =
        std::move(*bid.mutable_selected_buyer_and_seller_reporting_id());
  }
  ads_with_bids.emplace_back(std::move(ad_with_bid));

  return ads_with_bids;
}

void ExportLogsByType(
    absl::string_view ig_name,
    const google::protobuf::RepeatedPtrField<std::string>& logs,
    absl::string_view log_type, RequestLogContext& log_context) {
  if (!logs.empty()) {
    for (const std::string& log : logs) {
      std::string formatted_log =
          absl::StrCat("[", log_type, "] ", ig_name, ": ", log);
      PS_VLOG(kUdfLog, log_context) << formatted_log;
      log_context.SetEventMessageField(formatted_log);
    }
  }
}

size_t EstimateNumBids(
    const std::vector<std::vector<AdWithBid>>& ads_with_bids_by_ig) {
  size_t num_bids = 0;
  for (const auto& ads_with_bids : ads_with_bids_by_ig) {
    num_bids += ads_with_bids.size();
  }
  return num_bids;
}

void HandleLogMessages(absl::string_view ig_name,
                       const roma_service::LogMessages& log_messages,
                       RequestLogContext& log_context) {
  PS_VLOG(kUdfLog, log_context) << "UDF log messages for " << ig_name
                                << " exported in EventMessage if consented";
  ExportLogsByType(ig_name, log_messages.logs(), "log", log_context);
  ExportLogsByType(ig_name, log_messages.warnings(), "warning", log_context);
  ExportLogsByType(ig_name, log_messages.errors(), "error", log_context);
}

absl::Duration StringMsToAbslDuration(const std::string& string_ms) {
  absl::Duration duration;
  if (absl::ParseDuration(string_ms, &duration)) {
    return duration;
  }
  return kDefaultGenerateBidExecutionTimeout;
}
}  // namespace

GenerateBidsBinaryReactor::GenerateBidsBinaryReactor(
    grpc::CallbackServerContext* context,
    ByobDispatchClient<roma_service::GenerateProtectedAudienceBidRequest,
                       roma_service::GenerateProtectedAudienceBidResponse>&
        byob_client,
    const GenerateBidsRequest* request, GenerateBidsResponse* response,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BiddingServiceRuntimeConfig& runtime_config)
    : BaseGenerateBidsReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse>(
          runtime_config, request, response, key_fetcher_manager,
          crypto_client),
      context_(context),
      byob_client_(&byob_client),
      async_task_tracker_(raw_request_.interest_group_for_bidding_size(),
                          log_context_,
                          [this](bool any_successful_bid) {
                            OnAllBidsDone(any_successful_bid);
                          }),
      roma_timeout_duration_(StringMsToAbslDuration(roma_timeout_ms_)),
      auction_scope_(
          raw_request_.top_level_seller().empty()
              ? AuctionScope::AUCTION_SCOPE_SINGLE_SELLER
              : AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER) {
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::BiddingContextMap()->Remove(request_));
    if (log_context_.is_consented()) {
      metric_context_->SetConsented(raw_request_.log_context().generation_id());
    } else if (log_context_.is_prod_debug()) {
      metric_context_->SetConsented(kProdDebug.data());
    }
    return absl::OkStatus();
  }()) << "BiddingContextMap()->Get(request) should have been called";
}

void GenerateBidsBinaryReactor::Execute() {
  if (enable_cancellation_ && context_->IsCancelled()) {
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

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

  // Number of tasks to track equals the number of interest groups.
  int ig_count = raw_request_.interest_group_for_bidding_size();
  async_task_tracker_.SetNumTasksToTrack(ig_count);

  // Resize to number of interest groups in advance to prevent reallocation.
  ads_with_bids_by_ig_.resize(ig_count);

  // Send execution requests for each interest group immediately.
  start_binary_execution_time_ = absl::Now();

  roma_service::GenerateProtectedAudienceBidRequest bid_request =
      BuildProtectedAudienceBidRequest(
          raw_request_,
          enable_adtech_code_logging_ || !server_common::log::IsProd(),
          enable_buyer_debug_url_generation_ &&
              raw_request_.enable_debug_reporting());
  for (int ig_index = 0; ig_index < ig_count; ++ig_index) {
    ExecuteForInterestGroup(bid_request, ig_index);
  }
}

void GenerateBidsBinaryReactor::ExecuteForInterestGroup(
    roma_service::GenerateProtectedAudienceBidRequest& bid_request,
    int ig_index) {
  // Populate bid request for given interest group.
  UpdateProtectedAudienceBidRequest(
      bid_request, raw_request_,
      *raw_request_.mutable_interest_group_for_bidding(ig_index));
  const std::string ig_name = bid_request.interest_group().name();
  const bool logging_enabled = bid_request.server_metadata().logging_enabled();
  const bool debug_reporting_enabled =
      bid_request.server_metadata().debug_reporting_enabled();
  // Make asynchronous execute call using the BYOB client.
  PS_VLOG(kNoisyInfo) << "Starting UDF execution for IG: " << ig_name;
  absl::Status execute_status = byob_client_->Execute(
      bid_request, roma_timeout_duration_,
      [this, ig_index, ig_name, logging_enabled, debug_reporting_enabled](
          absl::StatusOr<roma_service::GenerateProtectedAudienceBidResponse>
              bid_response_status) mutable {
        // Error response.
        if (!bid_response_status.ok()) {
          PS_LOG(ERROR, log_context_)
              << "Execution of GenerateProtectedAudienceBid request failed "
                 "for IG "
              << ig_name << " with status: "
              << bid_response_status.status().ToString(
                     absl::StatusToStringMode::kWithEverything);
          if (bid_response_status.status().code() ==
              absl::StatusCode::kDeadlineExceeded) {
            // Execution timed out.
            LogIfError(
                metric_context_
                    ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                        1, metric::kBiddingGenerateBidsTimedOutError));
          } else {
            // Execution failed.
            LogIfError(
                metric_context_
                    ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                        1, metric::kBiddingGenerateBidsDispatchResponseError));
            LogIfError(
                metric_context_
                    ->AccumulateMetric<metric::kUdfExecutionErrorCount>(1));
          }
          async_task_tracker_.TaskCompleted(TaskStatus::ERROR);
          return;
        }

        // Empty response.
        roma_service::GenerateProtectedAudienceBidResponse& bid_response =
            *bid_response_status;
        if (!bid_response.IsInitialized() || bid_response.bids_size() == 0) {
          PS_LOG(INFO, log_context_)
              << "Execution of GenerateProtectedAudienceBid request for IG "
              << ig_name << "returned an empty response";
          async_task_tracker_.TaskCompleted(TaskStatus::EMPTY_RESPONSE);
          return;
        }

        // Successful response.
        if (server_common::log::PS_VLOG_IS_ON(kDispatch)) {
          PS_VLOG(kDispatch, log_context_)
              << "Generate Bids BYOB Response: " << bid_response;
        }
        // Populate list of AdsWithBids for this interest group from the bid
        // response returned by UDF.
        ads_with_bids_by_ig_[ig_index] = ParseProtectedAudienceBids(
            *bid_response.mutable_bids(), ig_name, debug_reporting_enabled);
        // Print log messages if present if logging enabled.
        if (logging_enabled && bid_response.has_log_messages()) {
          HandleLogMessages(ig_name, bid_response.log_messages(), log_context_);
        }
        async_task_tracker_.TaskCompleted(TaskStatus::SUCCESS);
      });

  // Handle the case where execute call fails to dispatch to UDF binary.
  if (!execute_status.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Execution of GenerateProtectedAudienceBid request failed "
           "for IG "
        << ig_name << " with status: "
        << execute_status.ToString(absl::StatusToStringMode::kWithEverything);
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                       1, metric::kBiddingGenerateBidsFailedToDispatchCode));
    async_task_tracker_.TaskCompleted(TaskStatus::ERROR);
  }
}

void GenerateBidsBinaryReactor::OnAllBidsDone(bool any_successful_bids) {
  // Log total end-to-end execution time.
  int binary_execution_time_ms =
      (absl::Now() - start_binary_execution_time_) / absl::Milliseconds(1);
  LogIfError(metric_context_->LogHistogram<metric::kUdfExecutionDuration>(
      binary_execution_time_ms));

  // Handle the case where none of the bid responses are successful.
  if (!any_successful_bids) {
    LogIfError(
        metric_context_->LogHistogram<metric::kBiddingFailedToBidPercent>(1.0));
    LogIfError(
        metric_context_->LogHistogram<metric::kBiddingTotalBidsCount>(0));
    PS_LOG(WARNING, log_context_)
        << "No successful bids returned by the adtech UDF";
    EncryptResponseAndFinish(grpc::Status(
        grpc::INTERNAL, "No successful bids returned by the adtech UDF"));
    return;
  }

  // Handle cancellation.
  if (enable_cancellation_ && context_->IsCancelled()) {
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  // Populate GenerateBidsRawResponse from list of valid AdsWithBids for all
  // interest groups.
  int failed_to_bid_count = 0;
  int received_bid_count = 0;
  int zero_bid_count = 0;
  int total_debug_urls_count = 0;
  long total_debug_urls_chars = 0;
  int rejected_component_bid_count = 0;
  raw_response_.mutable_bids()->Reserve(EstimateNumBids(ads_with_bids_by_ig_));
  for (std::vector<AdWithBid>& ads_with_bids : ads_with_bids_by_ig_) {
    if (ads_with_bids.empty()) {
      failed_to_bid_count += 1;
      continue;
    }
    for (AdWithBid& ad_with_bid : ads_with_bids) {
      received_bid_count += 1;
      if (absl::Status validation_status =
              IsValidProtectedAudienceBid(ad_with_bid, auction_scope_);
          !validation_status.ok()) {
        PS_VLOG(kNoisyInfo, log_context_) << validation_status.message();
        if (validation_status.code() == absl::StatusCode::kInvalidArgument) {
          zero_bid_count += 1;
        } else if (validation_status.code() ==
                   absl::StatusCode::kPermissionDenied) {
          // Received allowComponentAuction=false.
          ++rejected_component_bid_count;
        }
      } else {
        DebugUrlsSize debug_urls_size = TrimAndReturnDebugUrlsSize(
            ad_with_bid, max_allowed_size_debug_url_chars_,
            max_allowed_size_all_debug_urls_chars_, total_debug_urls_chars,
            log_context_);
        total_debug_urls_count += (debug_urls_size.win_url_chars > 0) +
                                  (debug_urls_size.loss_url_chars > 0);
        total_debug_urls_chars +=
            debug_urls_size.win_url_chars + debug_urls_size.loss_url_chars;
        *raw_response_.add_bids() = std::move(ad_with_bid);
      }
    }
  }
  LogIfError(metric_context_->LogHistogram<metric::kBiddingFailedToBidPercent>(
      (static_cast<double>(failed_to_bid_count)) /
      ads_with_bids_by_ig_.size()));
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
  LogIfError(metric_context_->LogUpDownCounter<metric::kBiddingDebugUrlCount>(
      total_debug_urls_count));
  LogIfError(metric_context_->LogHistogram<metric::kBiddingDebugUrlsSizeBytes>(
      static_cast<double>(total_debug_urls_chars)));
  EncryptResponseAndFinish(grpc::Status::OK);
}

void GenerateBidsBinaryReactor::EncryptResponseAndFinish(grpc::Status status) {
  raw_response_.set_bidding_export_debug(log_context_.ShouldExportEvent());
  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    PS_VLOG(kPlain, log_context_)
        << "GenerateBidsRawResponse exported in EventMessage if consented";
    log_context_.SetEventMessageField(raw_response_);
  }
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(
      /*if_export_consented=*/true, log_context_.ShouldExportEvent());

  if (!EncryptResponse()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to encrypt the generate bids response";
    status = grpc::Status(grpc::INTERNAL, kInternalServerError);
  }
  if (status.error_code() != grpc::StatusCode::OK) {
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  }
  PS_VLOG(kEncrypted, log_context_) << "Encrypted GenerateBidsResponse\n"
                                    << response_->ShortDebugString();
  Finish(status);
}

void GenerateBidsBinaryReactor::OnDone() { delete this; }

}  // namespace privacy_sandbox::bidding_auction_servers
