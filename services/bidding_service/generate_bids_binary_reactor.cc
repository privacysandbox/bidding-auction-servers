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
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/error_categories.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using IGForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;

inline constexpr int kDefaultGenerateBidExecutionTimeoutMs = 1000;

roma_service::GenerateProtectedAudienceBidRequest
BuildProtectedAudienceBidRequest(RawRequest& raw_request,
                                 IGForBidding& ig_for_bidding,
                                 bool logging_enabled) {
  roma_service::GenerateProtectedAudienceBidRequest bid_request;

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

  // Populate auction, buyer, and bidding signals.
  *bid_request.mutable_auction_signals() =
      std::move(*raw_request.mutable_auction_signals());
  *bid_request.mutable_per_buyer_signals() =
      std::move(*raw_request.mutable_buyer_signals());
  *bid_request.mutable_trusted_bidding_signals() =
      std::move(*ig_for_bidding.mutable_trusted_bidding_signals());

  // Populate (oneof) device signals.
  if (ig_for_bidding.has_android_signals() &&
      ig_for_bidding.android_signals().IsInitialized()) {
    roma_service::ProtectedAudienceAndroidSignals* android_signals =
        bid_request.mutable_android_signals();
    *android_signals->mutable_top_level_seller() =
        std::move(*raw_request.mutable_top_level_seller());
  } else if (ig_for_bidding.has_browser_signals() &&
             ig_for_bidding.browser_signals().IsInitialized()) {
    roma_service::ProtectedAudienceBrowserSignals* browser_signals =
        bid_request.mutable_browser_signals();
    *browser_signals->mutable_top_window_hostname() =
        std::move(*raw_request.mutable_publisher_name());
    *browser_signals->mutable_seller() =
        std::move(*raw_request.mutable_seller());
    *browser_signals->mutable_top_level_seller() =
        std::move(*raw_request.mutable_top_level_seller());
    browser_signals->set_join_count(
        ig_for_bidding.browser_signals().join_count());
    browser_signals->set_bid_count(
        ig_for_bidding.browser_signals().bid_count());
    if (ig_for_bidding.browser_signals().has_recency_ms()) {
      browser_signals->set_recency(
          ig_for_bidding.browser_signals().recency_ms());
    } else {
      browser_signals->set_recency(ig_for_bidding.browser_signals().recency() *
                                   1000);
    }
    *browser_signals->mutable_prev_wins() = std::move(
        *ig_for_bidding.mutable_browser_signals()->mutable_prev_wins());
  }

  // Populate server metadata.
  roma_service::ServerMetadata* server_metadata =
      bid_request.mutable_server_metadata();
  server_metadata->set_debug_reporting_enabled(
      raw_request.enable_debug_reporting());
  server_metadata->set_logging_enabled(logging_enabled);

  return bid_request;
}

std::vector<AdWithBid> ParseProtectedAudienceBids(
    google::protobuf::RepeatedPtrField<roma_service::ProtectedAudienceBid>&
        bids,
    absl::string_view ig_name) {
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
  if (bid.has_debug_report_urls()) {
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
  *ad_with_bid.mutable_buyer_reporting_id() =
      std::move(*bid.mutable_buyer_reporting_id());
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
          absl::StrFormat("[%s] %s: %s", log_type, ig_name, log);
      PS_VLOG(kUdfLog, log_context) << formatted_log;
      log_context.SetEventMessageField(formatted_log);
    }
  }
}

void HandleLogMessages(absl::string_view ig_name,
                       const roma_service::LogMessages& log_messages,
                       RequestLogContext& log_context) {
  PS_VLOG(kUdfLog, log_context)
      << "UDF log messages for " << ig_name << " exported in EventMessage";
  ExportLogsByType(ig_name, log_messages.logs(), "log", log_context);
  ExportLogsByType(ig_name, log_messages.warnings(), "warning", log_context);
  ExportLogsByType(ig_name, log_messages.errors(), "error", log_context);
}

absl::Duration StringMsToAbslDuration(const std::string& string_ms) {
  int64_t milliseconds;
  if (!absl::SimpleAtoi(string_ms, &milliseconds)) {
    milliseconds = kDefaultGenerateBidExecutionTimeoutMs;
  }
  return absl::Milliseconds(milliseconds);
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
    server_common::Executor* executor,
    const BiddingServiceRuntimeConfig& runtime_config)
    : BaseGenerateBidsReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse>(
          runtime_config, request, response, key_fetcher_manager,
          crypto_client),
      context_(context),
      byob_client_(byob_client),
      async_task_tracker_(raw_request_.interest_group_for_bidding_size(),
                          log_context_,
                          [this](bool any_successful_bid) {
                            OnAllBidsDone(any_successful_bid);
                          }),
      executor_(executor),
      roma_timeout_ms_duration_(StringMsToAbslDuration(roma_timeout_ms_)),
      auction_scope_(
          raw_request_.top_level_seller().empty()
              ? AuctionScope::AUCTION_SCOPE_SINGLE_SELLER
              : AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER) {
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::BiddingContextMap()->Remove(request_));
    if (log_context_.is_consented()) {
      metric_context_->SetConsented(raw_request_.log_context().generation_id());
    }
    return absl::OkStatus();
  }()) << "BiddingContextMap()->Get(request) should have been called";
  CHECK(executor_ != nullptr) << "Executor cannot be null";
}

void GenerateBidsBinaryReactor::Execute() {
  if (enable_cancellation_ && context_->IsCancelled()) {
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  PS_VLOG(kEncrypted, log_context_)
      << "Encrypted GenerateBidsRequest exported in EventMessage";
  log_context_.SetEventMessageField(*request_);
  PS_VLOG(kPlain, log_context_)
      << "GenerateBidsRawRequest exported in EventMessage";
  log_context_.SetEventMessageField(raw_request_);

  // Number of tasks to track equals the number of interest groups.
  int ig_count = raw_request_.interest_group_for_bidding_size();
  async_task_tracker_.SetNumTasksToTrack(ig_count);

  // Resize to number of interest groups in advance to prevent reallocation.
  ads_with_bids_by_ig_.resize(ig_count);

  // Send execution requests for each interest group in parallel.
  for (int ig_index = 0; ig_index < ig_count; ++ig_index) {
    executor_->Run([this, ig_index]() { ExecuteForInterestGroup(ig_index); });
  }
}

void GenerateBidsBinaryReactor::ExecuteForInterestGroup(int ig_index) {
  // Populate bid request for given interest group.
  const roma_service::GenerateProtectedAudienceBidRequest bid_request =
      BuildProtectedAudienceBidRequest(
          raw_request_,
          *raw_request_.mutable_interest_group_for_bidding(ig_index),
          enable_adtech_code_logging_ || !server_common::log::IsProd());

  // Make synchronous execute call using the BYOB client.
  absl::StatusOr<
      std::unique_ptr<roma_service::GenerateProtectedAudienceBidResponse>>
      bid_response =
          byob_client_.Execute(bid_request, roma_timeout_ms_duration_);

  // Error response.
  if (!bid_response.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Execution of GenerateProtectedAudienceBid request failed "
           "with status: "
        << bid_response.status();
    LogIfError(metric_context_
                   ->AccumulateMetric<metric::kBiddingErrorCountByErrorCode>(
                       1, metric::kBiddingGenerateBidsDispatchResponseError));
    async_task_tracker_.TaskCompleted(TaskStatus::ERROR);
    return;
  }

  // Empty response.
  if (!bid_response->get() || !bid_response->get()->IsInitialized() ||
      bid_response->get()->bids_size() == 0) {
    PS_LOG(INFO, log_context_)
        << "Execution of GenerateProtectedAudienceBid request returned "
           "an empty response.";
    // TODO(b/368624844): Add an EmptyResponse error code to
    // kBiddingErrorCountByErrorCount metric.
    async_task_tracker_.TaskCompleted(TaskStatus::EMPTY_RESPONSE);
    return;
  }

  // Successful response.
  // Populate list of AdsWithBids for this interest group from the bid response
  // returned by UDF.
  ads_with_bids_by_ig_[ig_index] =
      ParseProtectedAudienceBids(*bid_response->get()->mutable_bids(),
                                 bid_request.interest_group().name());
  // Print log messages if present if logging enabled.
  if (bid_request.server_metadata().logging_enabled() &&
      bid_response->get()->has_log_messages()) {
    HandleLogMessages(bid_request.interest_group().name(),
                      bid_response->get()->log_messages(), log_context_);
  }
  async_task_tracker_.TaskCompleted(TaskStatus::SUCCESS);
}

void GenerateBidsBinaryReactor::OnAllBidsDone(bool any_successful_bids) {
  // Handle the case where none of the bid responses are successful.
  if (!any_successful_bids) {
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
  int total_bid_count = 0;
  int zero_bid_count = 0;
  long total_debug_urls_chars = 0;
  for (std::vector<AdWithBid>& ads_with_bids : ads_with_bids_by_ig_) {
    for (AdWithBid& ad_with_bid : ads_with_bids) {
      total_bid_count += 1;
      if (absl::Status validation_status =
              IsValidProtectedAudienceBid(ad_with_bid, auction_scope_);
          !validation_status.ok()) {
        PS_VLOG(kNoisyWarn, log_context_) << validation_status.message();
        if (validation_status.code() == absl::StatusCode::kInvalidArgument) {
          zero_bid_count += 1;
          LogIfError(
              metric_context_->AccumulateMetric<metric::kBiddingZeroBidCount>(
                  1));
        }
      } else {
        total_debug_urls_chars += TrimAndReturnDebugUrlsSize(
            ad_with_bid, max_allowed_size_debug_url_chars_,
            max_allowed_size_all_debug_urls_chars_, total_debug_urls_chars,
            log_context_);
        *raw_response_.add_bids() = std::move(ad_with_bid);
      }
    }
  }
  // Increment the count of all bids per IG received from the UDF.
  LogIfError(metric_context_->AccumulateMetric<metric::kBiddingTotalBidsCount>(
      total_bid_count));
  // Log the percentage of zero bids per IG w.r.t. all bids per IG received from
  // the UDF.
  LogIfError(metric_context_->LogHistogram<metric::kBiddingZeroBidPercent>(
      (static_cast<double>(zero_bid_count)) / total_bid_count));

  EncryptResponseAndFinish(grpc::Status::OK);
}

void GenerateBidsBinaryReactor::EncryptResponseAndFinish(grpc::Status status) {
  PS_VLOG(kPlain, log_context_)
      << "GenerateBidsRawResponse exported in EventMessage";
  log_context_.SetEventMessageField(raw_response_);
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(/*if_export_consented=*/true);

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
