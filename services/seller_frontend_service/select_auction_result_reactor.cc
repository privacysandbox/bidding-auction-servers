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

#include "services/seller_frontend_service/select_auction_result_reactor.h"

#include "services/common/constants/user_error_strings.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/k_anon/k_anon_utils.h"
#include "services/seller_frontend_service/util/validation_utils.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

// Helper functions.
namespace {
absl::string_view GetGenerationId(
    const std::variant<ProtectedAudienceInput, ProtectedAuctionInput>& pai) {
  absl::string_view request_generation_id;
  std::visit(
      [&request_generation_id](const auto& protected_auction_input) {
        request_generation_id = protected_auction_input.generation_id();
      },
      pai);
  return request_generation_id;
}
}  // namespace

void SelectAuctionResultReactor::SetLoggingContextWithProtectedAuctionInput() {
  std::visit(
      [this](auto& protected_input) mutable {
        is_sampled_for_debug_ = SetGeneratorAndSample(
            config_client_.GetIntParameter(DEBUG_SAMPLE_RATE_MICRO),
            /*chaffing_enabled=*/false,
            request_->client_type() == CLIENT_TYPE_ANDROID &&
                protected_input.enable_unlimited_egress(),
            protected_input.generation_id(), generator_);

        if (config_client_.GetBooleanParameter(CONSENT_ALL_REQUESTS)) {
          ModifyConsent(*protected_input.mutable_consented_debug_config());
        }
        log_context_.Update(
            // -> absl::btree_map<std::string, std::string> {
            {
                {kGenerationId, protected_input.generation_id()},
                {kSellerDebugId, request_->auction_config().seller_debug_id()},
            },
            protected_input.consented_debug_config(), is_sampled_for_debug_);

        if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
          PS_VLOG(kPlain, log_context_)
              << "Headers:\n"
              << absl::StrJoin(
                     request_context_->client_metadata(), "\n",
                     absl::PairFormatter(absl::StreamFormatter(), " : ",
                                         absl::StreamFormatter()));
          log_context_.SetEventMessageField(protected_input);
          PS_VLOG(kPlain, log_context_)
              << (is_protected_auction_request_ ? "ProtectedAuctionInput"
                                                : "ProtectedAudienceInput")
              << " exported in EventMessage if consented" << "\n";
        }
      },
      protected_auction_input_);
}

void SelectAuctionResultReactor::LogRequestMetrics() {
  absl::string_view encapsulated_req;
  if (is_protected_auction_request_) {
    encapsulated_req = request_->protected_auction_ciphertext();
  } else {
    encapsulated_req = request_->protected_audience_ciphertext();
  }
  LogIfError(metric_context_->LogHistogram<metric::kProtectedCiphertextSize>(
      (int)encapsulated_req.size()));
  LogIfError(metric_context_->LogHistogram<metric::kAuctionConfigSize>(
      (int)request_->auction_config().ByteSizeLong()));
}

void SelectAuctionResultReactor::ScoreAds(
    std::vector<AuctionResult>& component_auction_results) {
  if (enable_cancellation_ && request_context_->IsCancelled()) {
    // TODO(b/359969718): update class cancellation wrapper to handle non-const
    // lvalue reference arguments
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> raw_request =
      CreateTopLevelScoreAdsRawRequest(request_->auction_config(),
                                       protected_auction_input_,
                                       component_auction_results);
  raw_request->set_is_sampled_for_debug(is_sampled_for_debug_);
  PS_VLOG(kOriginated, log_context_) << "\nScoreAdsRawRequest:\n"
                                     << raw_request->DebugString();
  auto auction_request_metric =
      metric::MakeInitiatedRequest(metric::kAs, metric_context_.get())
          .release();
  auction_request_metric->SetRequestSize((int)raw_request->ByteSizeLong());
  auto on_scoring_done = CancellationWrapper(
      request_context_, enable_cancellation_,
      [this, auction_request_metric](
          absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
              result,
          ResponseMetadata response_metadata) mutable {
        {
          int response_size =
              result.ok() ? (int)result->get()->ByteSizeLong() : 0;
          auction_request_metric->SetResponseSize(response_size);
          // destruct auction_request, destructor measures request time
          delete auction_request_metric;
        }
        OnScoreAdsDone(std::move(result));
      },  // OnCancel
      [this, auction_request_metric]() {
        delete auction_request_metric;
        FinishWithStatus(
            grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
      });

  grpc::ClientContext* client_context = client_contexts_.Add();

  absl::Status execute_result = clients_.scoring.ExecuteInternal(
      std::move(raw_request), client_context, std::move(on_scoring_done),
      absl::Milliseconds(
          config_client_.GetIntParameter(SCORE_ADS_RPC_TIMEOUT_MS)));
  if (!execute_result.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeScoreAdsFailedToCall));
    PS_LOG(ERROR, log_context_)
        << "Failed to make async ScoreAds call with error: "
        << execute_result.ToString();
    FinishWithStatus(grpc::Status(grpc::INTERNAL, kInternalServerError));
  }
}

void SelectAuctionResultReactor::OnScoreAdsDone(
    absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
        response) {
  PS_VLOG(kOriginated, log_context_)
      << "ScoreAdsResponse status:" << response.status();
  auto scoring_return_status = server_common::FromAbslStatus(response.status());
  if (!response.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeScoreAdsResponseError));
    LogIfError(metric_context_->AccumulateMetric<
               metric::kInitiatedRequestAuctionErrorCountByStatus>(
        1, StatusCodeToString(response.status().code())));
    // Any INTERNAL errors from auction service will be suppressed by SFE and
    // will cause a chaff to be sent back. Non-INTERNAL errors on the other hand
    // are propagated back the seller ad service.
    if (scoring_return_status.error_code() != grpc::StatusCode::INTERNAL) {
      FinishWithStatus(scoring_return_status);
      return;
    }
    // Continue sending a chaff response below if non-INTERNAL Error.
  } else {
    // Map AdScore to AuctionResult
    const auto& score_ad_response = *response;
    should_export_debug_ = score_ad_response->auction_export_debug();
    if (score_ad_response->has_debug_info()) {
      server_common::DebugInfo& auction_log =
          *response_->mutable_debug_info()->add_downstream_servers();
      auction_log = std::move(*score_ad_response->mutable_debug_info());
      auction_log.set_server_name("auction");
    }
    const bool has_winner = score_ad_response->has_ad_score() &&
                            score_ad_response->ad_score().buyer_bid() > 0;
    const bool has_ghost_winners =
        !score_ad_response->ghost_winning_ad_scores().empty();
    PS_VLOG(5, log_context_) << "has_winner: " << has_winner
                             << ", has_ghost_winners: " << has_ghost_winners
                             << ", enable_kanon_: " << enable_kanon_;
    if (has_winner || (enable_kanon_ && has_ghost_winners)) {
      if (has_winner) {
        // Set metric signals for winner, used to collect
        // metrics for requests with winners.
        metric_context_->SetCustomState(kWinningAuctionAd, "");
      }

      std::unique_ptr<KAnonAuctionResultData> kanon_data = nullptr;
      if (enable_kanon_ && has_ghost_winners) {
        kanon_data =
            std::make_unique<KAnonAuctionResultData>(GetKAnonAuctionResultData(
                has_winner ? score_ad_response->mutable_ad_score() : nullptr,
                *score_ad_response->mutable_ghost_winning_ad_scores(),
                log_context_));
      }

      FinishWithResponse(CreateNonChaffAuctionResultCiphertext(
          request_->auction_config().ad_auction_result_nonce(),
          score_ad_response->ad_score(),
          GetBuyerIgsWithBidsMap(component_auction_bidding_groups_),
          component_auction_update_groups_, request_->client_type(),
          *decrypted_request_, log_context_, std::move(kanon_data)));
      return;
    }
  }
  FinishWithResponse(CreateChaffAuctionResultCiphertext(
      request_->auction_config().ad_auction_result_nonce(),
      request_->client_type(), *decrypted_request_, log_context_));
}

void SelectAuctionResultReactor::FinishWithStatus(const grpc::Status& status) {
  if (status.error_code() != grpc::StatusCode::OK) {
    PS_LOG(ERROR, log_context_) << "RPC failed: " << status.error_message();
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  } else {
    PS_VLOG(kEncrypted, log_context_) << "Encrypted SelectAdResponse:\n"
                                      << response_->ShortDebugString();
  }
  if (metric_context_->CustomState(kWinningAuctionAd).ok()) {
    LogIfError(
        metric_context_->LogUpDownCounter<metric::kRequestWithWinnerCount>(1));
    LogIfError(metric_context_->LogHistogram<metric::kSfeWithWinnerTimeMs>(
        static_cast<int>((absl::Now() - start_) / absl::Milliseconds(1))));
  }
  log_context_.ExportEventMessage(/*if_export_consented=*/true,
                                  should_export_debug_);
  Finish(status);
}

void SelectAuctionResultReactor::FinishWithResponse(
    absl::StatusOr<std::string> auction_result_ciphertext) {
  if (!auction_result_ciphertext.ok()) {
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return;
  }
  *response_->mutable_auction_result_ciphertext() =
      std::move(*auction_result_ciphertext);
  FinishWithStatus(grpc::Status::OK);
}

void SelectAuctionResultReactor::FinishWithClientVisibleErrors(
    absl::string_view message) {
  PS_LOG(ERROR, log_context_)
      << "Finishing the SelectAdRequest RPC with client visible error: "
      << message;
  AuctionResult::Error auction_error;
  auction_error.set_code(static_cast<int>(ErrorCode::CLIENT_SIDE));
  auction_error.set_message(message);
  FinishWithResponse(CreateErrorAuctionResultCiphertext(
      request_->auction_config().ad_auction_result_nonce(), auction_error,
      request_->client_type(), *decrypted_request_, log_context_));
}

void SelectAuctionResultReactor::FinishWithServerVisibleErrors(
    absl::string_view message) {
  PS_LOG(ERROR, log_context_)
      << "Finishing the SelectAdRequest RPC with ad server visible error";
  FinishWithStatus(
      grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, message.data()));
}

void SelectAuctionResultReactor::Execute() {
  // Do not go further if server is misconfigured.
  if (seller_domain_.empty()) {
    FinishWithServerVisibleErrors(kSellerDomainEmpty);
    return;
  }

  LogRequestMetrics();
  // Perform Validation on request.
  if (!ValidateEncryptedSelectAdRequest(
          *request_, AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER,
          seller_domain_, error_accumulator_)) {
    FinishWithServerVisibleErrors(error_accumulator_.GetAccumulatedErrorString(
        ErrorVisibility::AD_SERVER_VISIBLE));
    return;
  }

  // Decrypt and set PAI.
  absl::string_view encapsulated_req;
  if (is_protected_auction_request_) {
    encapsulated_req = request_->protected_auction_ciphertext();
  } else {
    encapsulated_req = request_->protected_audience_ciphertext();
  }
  auto decrypt_req_status = DecryptOHTTPEncapsulatedHpkeCiphertext(
      encapsulated_req, clients_.key_fetcher_manager_);
  // Could not decrypt PAI.
  if (!decrypt_req_status.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Error decrypting the protected "
        << (is_protected_auction_request_ ? "auction" : "audience")
        << " input ciphertext" << decrypt_req_status.status();
    // Client side errors.
    FinishWithClientVisibleErrors(
        absl::StrFormat(kErrorDecryptingCiphertextError,
                        decrypt_req_status.status().message()));
    return;
  }
  decrypted_request_ = std::move(*decrypt_req_status);

  if (is_protected_auction_request_) {
    protected_auction_input_ = DecryptProtectedAuctionInput(
        decrypted_request_->plaintext, clients_.key_fetcher_manager_,
        request_->client_type(), error_accumulator_);
  } else {
    protected_auction_input_ = DecryptProtectedAudienceInput(
        decrypted_request_->plaintext, clients_.key_fetcher_manager_,
        request_->client_type(), error_accumulator_);
  }

  // Populate the logging context needed for request tracing.
  // If decryption fails, we still want to log the request and
  // header with empty context.
  SetLoggingContextWithProtectedAuctionInput();
  // Log Request after log context is set. If decryption fails, we still
  // want to log the request and header with empty context.
  PS_VLOG(kEncrypted, log_context_) << "Encrypted SelectAdRequest:\n"
                                    << request_->ShortDebugString();

  // Validate PAI.
  request_generation_id_ = GetGenerationId(protected_auction_input_);
  if (request_generation_id_.empty()) {
    PS_LOG(ERROR, log_context_) << kMissingGenerationId;
    // Client side errors.
    FinishWithClientVisibleErrors(kMissingGenerationId);
    return;
  }

  // Decrypt and validate AuctionResults.
  std::vector<AuctionResult> component_auction_results =
      DecryptAndValidateComponentAuctionResults(
          request_, seller_domain_, request_generation_id_,
          *clients_.crypto_client_ptr_, clients_.key_fetcher_manager_,
          error_accumulator_, log_context_);

  // No valid auction results found.
  if (component_auction_results.empty()) {
    std::string error_msg = error_accumulator_.GetAccumulatedErrorString(
        ErrorVisibility::AD_SERVER_VISIBLE);
    PS_LOG(ERROR, log_context_) << error_msg;
    FinishWithServerVisibleErrors(error_msg);
    return;
  }

  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    log_context_.SetEventMessageField(component_auction_results);
    PS_VLOG(kPlain, log_context_)
        << "component auction results exported in EventMessage if consented";
  }

  // Keep bidding groups for adding to response.
  for (auto& car : component_auction_results) {
    component_auction_bidding_groups_.push_back(
        std::move(*car.mutable_bidding_groups()));

    UpdateGroupMap update_group_map = std::move(*car.mutable_update_groups());
    for (auto& [ig_owner, updates] : update_group_map) {
      component_auction_update_groups_.emplace(ig_owner, std::move(updates));
    }
  }

  // Map and Call Auction Service.
  ScoreAds(component_auction_results);
}

void SelectAuctionResultReactor::OnDone() { delete this; }

void SelectAuctionResultReactor::OnCancel() { client_contexts_.CancelAll(); }

SelectAuctionResultReactor::SelectAuctionResultReactor(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client, bool enable_cancellation,
    bool enable_buyer_private_aggregate_reporting,
    int per_adtech_paapi_contributions_limit, bool enable_kanon)
    : request_context_(context),
      request_(request),
      response_(response),
      is_protected_auction_request_(
          !request_->protected_auction_ciphertext().empty()),
      clients_(clients),
      config_client_(config_client),
      log_context_({}, server_common::ConsentedDebugConfiguration(),
                   [this]() { return response_->mutable_debug_info(); }),
      error_accumulator_(&log_context_),
      enable_cancellation_(enable_cancellation),
      enable_kanon_(enable_kanon) {
  seller_domain_ = config_client_.GetStringParameter(SELLER_ORIGIN_DOMAIN);
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::SfeContextMap()->Remove(request_));
    return absl::OkStatus();
  }()) << "SfeContextMap()->Get(request) should have been called";
}

}  // namespace privacy_sandbox::bidding_auction_servers
