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
#include "services/common/loggers/build_input_process_response_benchmarking_logger.h"
#include "services/common/loggers/no_ops_logger.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/error_reporter.h"
#include "services/common/util/request_metadata.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/seller_frontend_service.h"

namespace privacy_sandbox::bidding_auction_servers {
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
    kBuyerMetadataKeysMap = {{{"x-accept-language", "x-accept-language"},
                              {"x-user-agent", "x-user-agent"},
                              {"x-bna-client-ip", "x-bna-client-ip"}}};

// Constants for user errors.
inline constexpr char kEmptyRemarketingCiphertextError[] =
    "remarketing_ciphertext must be non-null.";
inline constexpr char kInvalidOhttpKeyIdError[] =
    "Invalid key ID provided in OHTTP encapsulated request for "
    "protected_audience_ciphertext.";
inline constexpr char kMissingPrivateKey[] =
    "Unable to get private key for the key ID in OHTTP encapsulated request.";
inline constexpr char kUnsupportedClientType[] = "Unsupported client type.";
inline constexpr char kMalformedEncapsulatedRequest[] =
    "Malformed OHTTP encapsulated request provided for "
    "protected_audience_ciphertext: "
    "%s";

// Constants for bad Ad server provided inputs.
inline constexpr char kEmptySellerSignals[] =
    "Seller signals missing in auction config";
inline constexpr char kEmptyAuctionSignals[] =
    "Auction signals missing in auction config";
inline constexpr char kEmptyBuyerList[] = "No buyers specified";
inline constexpr char kEmptySeller[] =
    "Seller origin missing in auction config";
inline constexpr char kEmptyBuyerSignals[] =
    "Buyer signals missing in auction config for buyer: %s";
inline constexpr char kUnknownClientType[] =
    "Unknown client type in SelectAdRequest";
inline constexpr char kWrongSellerDomain[] =
    "Seller domain passed in request does not match this server's domain";
inline constexpr char kEmptyBuyerInPerBuyerConfig[] =
    "One or more buyer keys are empty in per buyer config map";

inline constexpr char kNoBidsReceived[] = "No bids received.";

// Struct for any objects needed throughout the lifecycle of the request.
struct RequestContext {
  // Key ID used to encrypt the request.
  google::scp::cpio::PublicPrivateKeyPairId key_id;

  // OHTTP context generated during request decryption.
  std::unique_ptr<quiche::ObliviousHttpRequest::Context> context;

  server_common::PrivateKey private_key;
};

enum class CompletedBidState {
  UNKNOWN,
  SKIPPED,         // Missing data related to the buyer, hence BFE call skipped.
  EMPTY_RESPONSE,  // Buyer returned no response for the GetBid call.
  ERROR,
  SUCCESS,
};

struct BidStats {
  const int initial_bids_count;
  int pending_bids_count;
  int successful_bids_count;
  int empty_bids_count;
  int skipped_bids_count;
  int error_bids_count;
  explicit BidStats(int initial_bids_count_input);

  void BidCompleted(CompletedBidState completed_bid_state);
  // Indicates whether any bid was successful or if no buyer returned an empty
  // bid so that we should send a chaff back. This should be called after all
  // the get bid calls to buyer frontend have returned.
  bool HasAnySuccessfulBids();
  std::string ToString();
};

// This is a gRPC reactor that serves a single GenerateBidsRequest.
// It stores state relevant to the request and after the
// response is finished being served, SelectAdReactor cleans up all
// necessary state and grpc releases the reactor from memory.
class SelectAdReactor : public grpc::ServerUnaryReactor {
 public:
  explicit SelectAdReactor(grpc::CallbackServerContext* context,
                           const SelectAdRequest* request,
                           SelectAdResponse* response,
                           const ClientRegistry& clients,
                           const TrustedServersConfigClient& config_client,
                           bool fail_fast = true);

  // Initiate the asynchronous execution of the SelectingWinningAdRequest.
  virtual void Execute();

 protected:
  using ErrorHandlerSignature = const std::function<void(absl::string_view)>&;

  // Gets a string representing the response to be returned to the client. This
  // data will be encrypted before it is sent back to the client.
  virtual absl::StatusOr<std::string> GetNonEncryptedResponse(
      const std::optional<ScoreAdsResponse::AdScore>& high_score,
      const google::protobuf::Map<
          std::string, AuctionResult::InterestGroupIndex>& bidding_group_map,
      const std::optional<AuctionResult::Error>& error) = 0;

  // Decodes the plaintext payload and returns a `ProtectedAudienceInput` proto.
  // Any errors while decoding are reported to error accumulator object.
  virtual ProtectedAudienceInput GetDecodedProtectedAudienceInput(
      absl::string_view encoded_data) = 0;

  // Returns the decoded BuyerInput from the encoded/compressed BuyerInput.
  // Any errors while decoding are reported to error accumulator object.
  virtual absl::flat_hash_map<absl::string_view, BuyerInput>
  GetDecodedBuyerinputs(const google::protobuf::Map<std::string, std::string>&
                            encoded_buyer_inputs) = 0;

  // Checks if any client visible errors have been observed.
  bool HaveClientVisibleErrors();

  // Checks if any ad server visible errors have been observed.
  bool HaveAdServerVisibleErrors();

  // Finishes the RPC call while reporting the error.
  void FinishWithInternalError(absl::string_view error);

  // Validates the mandatory fields in the request. Reports any errors to the
  // error accumulator.
  void ValidateProtectedAudienceInput(
      const ProtectedAudienceInput& protected_audience_input);

  // Populates the errors that need to be sent to the client (in the encrypted
  // response).
  void MayPopulateClientVisibleErrors();

  // Populates the errors related to bad inputs from ad server.
  void MayPopulateAdServerVisibleErrors();

  // Gets a string of all errors caused by bad inputs to the SFE.
  std::string GetAccumulatedErrorString(ErrorVisibility error_visibility);

  // Inspects errors that have been accumulated so far and then finishes the
  // request (either with OK status or an error status depending on the errors).
  // This must only be called when some errors have been observed previously.
  void SetErrorsAndFinishRequest();

  // Decrypts the ProtectedAudienceInput in the request object and returns
  // whether decryption was successful.
  bool DecryptRequest();

  // Fetches the bids from a single buyer by initiating an asynchronous GetBids
  // rpc.
  //
  // buyer: a string representing the buyer, identified as an IG owner.
  // buyer_input: input for bidding.
  void FetchBid(const std::string& buyer_ig_owner,
                const BuyerInput& buyer_input, absl::string_view seller);
  // Handles recording the fetched bid to state.
  // This is called by the grpc buyer client when the request is finished,
  // and will subsequently call UpdatePendingBidsState which will update how
  // many bids are still pending and finally fetch the scoring signals.
  //
  // response: an error status or response from the GetBid request.
  // buyer_hostname: the hostname of the buyer
  void OnFetchBidsDone(
      absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
          response,
      const std::string& buyer_hostname) ABSL_LOCKS_EXCLUDED(bid_data_mu_);

  // Updates the state, keeping track of how many bids are still pending.
  // Once the pending bid count reaches zero, we initiate a call
  // to FetchScoringSignals.
  void UpdatePendingBidsState(CompletedBidState completed_bid_state)
      ABSL_LOCKS_EXCLUDED(bid_data_mu_);

  // Initiates the asynchronous grpc request to fetch scoring signals
  // from the key value server. The ad_render_url in the GetBid response from
  // each Buyer is used as a key for the Seller Key-Value lookup.
  void FetchScoringSignals();

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
  void ScoreAds();

  // Handles the auction result and writes the winning ad to
  // the SelectAdResponse, thus finishing the SelectAdRequest.
  // This function is called by the auction service client as a done callback.
  //
  // status: the status of the grpc request if failed or the ScoreAdsResponse,
  // which contains the scores for each ad in the auction.
  void OnScoreAdsDone(
      absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
          status);

  // Gets the bidding groups after scoring is done.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
  GetBiddingGroups();

  // Sends debug reporting pings to buyers for the interest groups.
  void PerformDebugReporting(
      const std::optional<ScoreAdsResponse::AdScore>& high_score);

  // Populates the raw AuctionResult response.
  void PopulateRawResponse(
      const std::optional<ScoreAdsResponse::AdScore>& high_score,
      google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
          bidding_group_map);

  // Encrypts the AuctionResult and sets the ciphertext field in the response.
  // Returns whether encryption was successful.
  bool EncryptResponse(std::string plaintext_response);

  // Cleans up and deletes the SelectAdReactor. Called by the grpc library after
  // the response has finished.
  void OnDone() override;

  // Abandons the entire SelectAdRequest. Called by the grpc library if the
  // client cancels the request.
  void OnCancel() override;

  // Finishes the RPC call with an OK status.
  void FinishWithOkStatus();

  // Populates the logging context needed for request tracing. For the case when
  // encrypting is enabled, this method should be called after decrypting
  // and decoding the request.
  ContextLogger::ContextMap GetLoggingContext();

  // Reports an error to the error accumulator object.
  void ReportError(
      ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
      const std::string& msg, ErrorCode error_code);

  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata BuildAdWithBidMetadata(
      const AdWithBid& input, absl::string_view interest_group_owner);

  // Initialization
  grpc::CallbackServerContext* context_;
  const SelectAdRequest* request_;
  ProtectedAudienceInput protected_audience_input_;
  SelectAdResponse* response_;
  AuctionResult::Error error_;
  const ClientRegistry& clients_;
  const TrustedServersConfigClient& config_client_;

  // Key Value Fetch Result.
  std::unique_ptr<ScoringSignals> scoring_signals_;

  // Metadata to be sent to buyers.
  RequestMetadata buyer_metadata_;

  // Store references to bids results for mutex free single thread operations.
  BuyerBidsList buyer_bids_list_;

  // Benchmarking Logger to benchmark the service
  std::unique_ptr<BenchmarkingLogger> benchmarking_logger_;

  // Get Bid Results
  // Multiple threads can be writing buyer bid responses, so we use a
  // mutex guard.
  absl::Mutex bid_data_mu_;
  absl::flat_hash_map<std::string,
                      std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
      buyer_bids_ ABSL_GUARDED_BY(bid_data_mu_);

  // Request context needed throughout the lifecycle of the request.
  RequestContext request_context_;
  // Logger that logs enough context around a request so that it can be traced
  // through B&A services.
  ContextLogger logger_;

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

 private:
  // Keeps track of how many buyer bids were expected initially and how many
  // were erroneous. If all bids ended up in an error state then that should be
  // flagged as an error eventually.
  BidStats bid_stats_ ABSL_GUARDED_BY(bid_data_mu_);

  // Log metrics for the requests that were initiated by the server
  void LogInitiatedRequestMetrics(int initiated_request_duration_ms);
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELECT_WINNING_AD_REACTOR_H_
