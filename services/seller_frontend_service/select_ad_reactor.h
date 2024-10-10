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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_H_

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/flags/flag.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "include/grpcpp/impl/codegen/server_callback.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/feature_flags.h"
#include "services/common/loggers/build_input_process_response_benchmarking_logger.h"
#include "services/common/loggers/no_ops_logger.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/async_task_tracker.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/client_contexts.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/error_reporter.h"
#include "services/common/util/request_metadata.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/util/encryption_util.h"
#include "services/seller_frontend_service/util/validation_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
    kBuyerMetadataKeysMap = {{{"x-accept-language", "x-accept-language"},
                              {"x-user-agent", "x-user-agent"},
                              {"x-bna-client-ip", "x-bna-client-ip"}}};

// Constants for service errors.
inline constexpr char kInvalidBuyerCurrency[] = "Invalid Buyer Currency";
inline constexpr char kInvalidSellerCurrency[] = "Invalid Seller Currency";

inline constexpr char kNoBidsReceived[] = "No bids received.";

inline constexpr char kAllBidsRejectedBuyerCurrencyMismatch[] =
    "All bids rejected for failure to match buyer currency.";

inline constexpr absl::string_view kWinningAd = "winning_ad";

inline constexpr int kMinChaffRequestSizeBytes = 9000;
inline constexpr int kMaxChaffRequestSizeBytes = 95000;

inline constexpr int kMinChaffRequests = 1;
inline constexpr int kMinChaffRequestsWithNoRealRequests = 2;

// Maximum number of buyers that can be sent requests when chaffing is enabled.
inline constexpr int kMaxBuyersSolicitedChaffingEnabled = 15;

struct ChaffingConfig {
  absl::flat_hash_set<std::string_view> chaff_request_candidates;
  int num_chaff_requests = 0;
  int num_real_requests = 0;
};

// This is a gRPC reactor that serves a single GenerateBidsRequest.
// It stores state relevant to the request and after the
// response is finished being served, SelectAdReactor cleans up all
// necessary state and grpc releases the reactor from memory.
class SelectAdReactor : public grpc::ServerUnaryReactor {
 public:
  explicit SelectAdReactor(
      grpc::CallbackServerContext* context, const SelectAdRequest* request,
      SelectAdResponse* response, const ClientRegistry& clients,
      const TrustedServersConfigClient& config_client, bool fail_fast = true,
      int max_buyers_solicited = metric::kMaxBuyersSolicited);

  // Initiate the asynchronous execution of the SelectAdRequest.
  virtual void Execute();

 protected:
  using ErrorHandlerSignature = const std::function<void(absl::string_view)>&;
  using AuctionConfig = SelectAdRequest::AuctionConfig;

  // Gets a string representing the response to be returned to the client. This
  // data will be encrypted before it is sent back to the client.
  virtual absl::StatusOr<std::string> GetNonEncryptedResponse(
      const std::optional<ScoreAdsResponse::AdScore>& high_score,
      const std::optional<AuctionResult::Error>& error) = 0;

  // Decodes the plaintext payload and returns a `ProtectedAudienceInput` proto.
  // Any errors while decoding are reported to error accumulator object.
  [[deprecated]] virtual ProtectedAudienceInput
  GetDecodedProtectedAudienceInput(absl::string_view encoded_data) = 0;

  // Decodes the plaintext payload and returns a `ProtectedAuctionInput` proto.
  // Any errors while decoding are reported to error accumulator object.
  virtual ProtectedAuctionInput GetDecodedProtectedAuctionInput(
      absl::string_view encoded_data) = 0;

  // Returns the decoded BuyerInput from the encoded/compressed BuyerInput.
  // Any errors while decoding are reported to error accumulator object.
  virtual absl::flat_hash_map<absl::string_view, BuyerInput>
  GetDecodedBuyerinputs(const google::protobuf::Map<std::string, std::string>&
                            encoded_buyer_inputs) = 0;

  virtual std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
  CreateGetBidsRequest(const std::string& buyer_ig_owner,
                       const BuyerInput& buyer_input);

  virtual std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
  CreateScoreAdsRequest();

  // Checks if any client visible errors have been observed.
  bool HaveClientVisibleErrors();

  // Checks if any ad server visible errors have been observed.
  bool HaveAdServerVisibleErrors();

  // Checks that all AdWithBids match the specified buyer_currency, and throws
  // out those which do not match.
  // PRECONDITION 1: shared_buyer_bids_map_ must have at least one entry.
  // PRECONDITION 2: each buyer in shared_buyer_bids_map must be non-empty.
  // RETURNS: True if any bids remain to be scored and false otherwise.
  bool FilterBidsWithMismatchingCurrency();

  template <typename T>
  void FilterBidsWithMismatchingCurrencyHelper(
      google::protobuf::RepeatedPtrField<T>* ads_with_bids,
      absl::string_view buyer_currency);

  // Validates the mandatory fields in the request. Reports any errors to the
  // error accumulator.
  template <typename T>
  void ValidateProtectedAuctionInput(const T& protected_auction_input) {
    if (protected_auction_input.generation_id().empty()) {
      ReportError(ErrorVisibility::CLIENT_VISIBLE, kMissingGenerationId,
                  ErrorCode::CLIENT_SIDE);
    }

    if (protected_auction_input.publisher_name().empty()) {
      ReportError(ErrorVisibility::CLIENT_VISIBLE, kMissingPublisherName,
                  ErrorCode::CLIENT_SIDE);
    }

    // Validate Buyer Inputs.
    if (buyer_inputs_->empty()) {
      ReportError(ErrorVisibility::CLIENT_VISIBLE, kMissingBuyerInputs,
                  ErrorCode::CLIENT_SIDE);
    } else {
      bool is_any_buyer_input_valid = false;
      std::set<std::string> observed_errors;
      for (const auto& [buyer, buyer_input] : *buyer_inputs_) {
        bool any_error = false;
        if (buyer.empty()) {
          observed_errors.insert(kEmptyInterestGroupOwner);
          any_error = true;
        }
        if (buyer_input.interest_groups().empty() &&
            !buyer_input.has_protected_app_signals()) {
          observed_errors.insert(absl::StrFormat(
              kMissingInterestGroupsAndProtectedSignals, buyer));
          any_error = true;
        }
        if (any_error) {
          continue;
        }
        is_any_buyer_input_valid = true;
      }
      // Buyer inputs have keys but none of the key/value pairs are usable to
      // get bids from buyers.
      if (!is_any_buyer_input_valid) {
        std::string error =
            absl::StrFormat(kNonEmptyBuyerInputMalformed,
                            absl::StrJoin(observed_errors, kErrorDelimiter));
        ReportError(ErrorVisibility::CLIENT_VISIBLE, error,
                    ErrorCode::CLIENT_SIDE);
      } else {
        // Log but don't report the errors for malformed buyer inputs because we
        // have found at least one buyer input that is well formed.
        for (const auto& observed_error : observed_errors) {
          PS_VLOG(kNoisyWarn, log_context_) << observed_error;
        }
      }
    }
  }

  // Logs the decoded buyer inputs if available.
  void MayLogBuyerInput();

  // Populates the errors that need to be sent to the client (in the encrypted
  // response).
  void MayPopulateClientVisibleErrors();

  // Populates the errors related to bad inputs from ad server.
  void MayPopulateAdServerVisibleErrors();

  // Gets a string of all errors caused by bad inputs to the SFE.
  std::string GetAccumulatedErrorString(ErrorVisibility error_visibility);

  // Decrypts the ProtectedAudienceInput in the request object and returns
  // whether decryption was successful.
  grpc::Status DecryptRequest();

  // Computes the number of buyers that can be invoked by looking at the request
  // and whether chaffing is enabled on the server. For servers where chaffing
  // is disabled, the number of effective buyers is the number of buyers that
  // are present in all 3 of the following:
  //  1) the auction_config.buyer_list,
  //  2) the buyer_inputs, and
  //  3) the SFE config for which buyers it can send requests to (the
  //  BUYER_SERVER_HOSTS flag)
  // For servers where chaffing is disabled, an additional 'n' buyers can be
  // invoked for sending chaff requests. See GetChaffingConfig() for the logic.
  int GetEffectiveNumberOfBuyers(const ChaffingConfig& chaffing_config);

  // Dispatches the GetBids calls for both 'real' and 'fake' (AKA chaff) buyers.
  void FetchBids();

  // Fetches the bids from a single buyer by initiating an asynchronous GetBids
  // rpc.
  //
  // buyer: a string representing the buyer, identified as an IG owner.
  // get_bids_request: the GetBids request proto for the specified buyer.
  void FetchBid(
      const std::string& buyer_ig_owner,
      std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request);

  // Handles recording the fetched bid to state.
  // This is called by the grpc buyer client when the request is finished,
  // and will subsequently call update pending bids state which will update how
  // many bids are still pending and finally fetch the scoring signals once all
  // bids are done.
  //
  // response: an error status or response from the GetBid request.
  // buyer_hostname: the hostname of the buyer
  void OnFetchBidsDone(
      absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
          response,
      const std::string& buyer_hostname);

  // Calls FetchScoringSignals or calls Finish on the reactor depending on
  // if there were any successful bids or if the request was cancelled by the
  // client.
  void OnAllBidsDone(bool any_successful_bids);

  // Initiates the asynchronous grpc request to fetch scoring signals
  // from the key value server. The ad_render_url in the GetBid response from
  // each Buyer is used as a key for the Seller Key-Value lookup.
  void CancellableFetchScoringSignals();

  // Handles recording the fetched scoring signals to state.
  // If the code blob is already fetched, this function initiates scoring the
  // auction.
  //
  // result: the status or the  GetValuesClientOutput, which contains the
  // scoring signals that will be used by the auction service.
  void OnFetchScoringSignalsDone(
      absl::StatusOr<std::unique_ptr<ScoringSignals>> result);

  // Initiates an asynchronous rpc to the auction service. This request includes
  // all signals and bids.
  void CancellableScoreAds();

  // Handles the auction result and writes the winning ad to
  // the SelectAdResponse, thus finishing the SelectAdRequest.
  // This function is called by the auction service client as a done callback.
  //
  // status: the status of the grpc request if failed or the ScoreAdsResponse,
  // which contains the scores for each ad in the auction.
  void OnScoreAdsDone(
      absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
          status);

  // Sends debug reporting pings to buyers for the interest groups.
  void PerformDebugReporting(
      const std::optional<ScoreAdsResponse::AdScore>& high_score);

  // Encrypts the AuctionResult and sets the ciphertext field in the response.
  // Returns whether encryption was successful.
  bool EncryptResponse(std::string plaintext_response);

  // Cleans up and deletes the SelectAdReactor. Called by the grpc library after
  // the response has finished.
  void OnDone() override;

  // Abandons the entire SelectAdRequest. Called by the grpc library if the
  // client cancels the request.
  void OnCancel() override;

  // Finishes the RPC call with a status.
  void FinishWithStatus(const grpc::Status& status);

  // Reports an error to the error accumulator object.
  void ReportError(
      log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
      const std::string& msg, ErrorCode error_code);

  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata BuildAdWithBidMetadata(
      const AdWithBid& input, absl::string_view interest_group_owner,
      bool k_anon_status);

  bool GetKAnonStatusForAdWithBid(absl::string_view ad_key);

  CLASS_CANCELLATION_WRAPPER(FetchScoringSignals, enable_cancellation_,
                             request_context_, FinishWithStatus)
  CLASS_CANCELLATION_WRAPPER(ScoreAds, enable_cancellation_, request_context_,
                             FinishWithStatus)

  // Initialization
  grpc::CallbackServerContext* request_context_;
  const SelectAdRequest* request_;
  std::variant<ProtectedAudienceInput, ProtectedAuctionInput>
      protected_auction_input_;
  SelectAdResponse* response_;
  AuctionResult::Error error_;
  const ClientRegistry& clients_;
  const TrustedServersConfigClient& config_client_;
  // Scope for current auction (single seller, top level or component)
  const AuctionScope auction_scope_;

  // Key Value Fetch Result.
  std::unique_ptr<ScoringSignals> scoring_signals_;

  // Metadata to be sent to buyers.
  RequestMetadata buyer_metadata_;

  // Get Bid Results
  // Multiple threads can be writing buyer bid responses so this map
  // gets locked when async_task_tracker_ updates the state of pending bids.
  // The map can be freely used without a lock after all the bids have
  // completed.
  BuyerBidsResponseMap shared_buyer_bids_map_;

  // Similar to BuyerBidsResponseMap, but holds copies of
  // UpdateInterestGroupList and can represent buyers that have returned 0 bids.
  // Multiple threads can be writing buyer ig update responses
  // so this map gets locked when async_task_tracker_ updates the state of
  // pending bids. The map can be freely used without a lock after all pending
  // GetBidsResponses have completed -- fields from the map, and the map itself,
  // may be moved in order to build the response.
  UpdateGroupMap shared_ig_updates_map_;

  // Benchmarking Logger to benchmark the service
  std::unique_ptr<BenchmarkingLogger> benchmarking_logger_;

  // Encryption context needed throughout the lifecycle of the request.
  std::unique_ptr<OhttpHpkeDecryptedMessage> decrypted_request_;

  RequestLogContext log_context_;

  // Decompressed and decoded buyer inputs.
  absl::StatusOr<absl::flat_hash_map<absl::string_view, BuyerInput>>
      buyer_inputs_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::SfeContext> metric_context_;

  // Object that accumulates all the errors and aggregates them based on their
  // intended visibility.
  ErrorAccumulator error_accumulator_;
  // Bool indicating whether or not the reactor should bail out on first input
  // validation failure. Setting this to false can help reduce the debugging
  // time on the client side since clients will get a holistic report of what is
  // wrong with each input field.
  const bool fail_fast_;

  // Indicates whether the request is using newer request field.
  // This can be removed once all the clients start using this new field.
  bool is_protected_auction_request_;

  // Indicates whether or not the protected app signals feature is enabled or
  // not.
  const bool is_pas_enabled_;

  // Indicates whether or not the protected audience support is enabled.
  const bool is_protected_audience_enabled_;

  // Is chaffing enabled on the server.
  const bool chaffing_enabled_;

  // Temporary workaround for compliance, will be removed (b/308032414).
  const int max_buyers_solicited_;

  const bool enable_cancellation_;

  const bool enable_kanon_;

  bool enforce_kanon_;

  // Pseudo random number generator for use in chaffing.
  std::optional<std::mt19937> generator_;

 private:
  // Keeps track of how many buyer bids were expected initially and how many
  // were erroneous. If all bids ended up in an error state then that should be
  // flagged as an error eventually.
  AsyncTaskTracker async_task_tracker_;

  // Keeps track of the client contexts used for RPC calls
  ClientContexts client_contexts_;

  // Log metrics for the Initiated requests errors that were initiated by the
  // server
  void LogInitiatedRequestErrorMetrics(absl::string_view server_name,
                                       const absl::Status& status,
                                       absl::string_view buyer = "");

  ChaffingConfig GetChaffingConfig(
      const absl::flat_hash_set<absl::string_view>& auction_config_buyer_set);

  absl::Time start_ = absl::Now();
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELECT_WINNING_AD_REACTOR_H_
