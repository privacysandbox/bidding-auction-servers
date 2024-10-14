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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_PROTO_MAPPING_UTIL_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_PROTO_MAPPING_UTIL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/compression/gzip.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/error_categories.h"
#include "services/common/util/hpke_utils.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/util/encryption_util.h"
#include "services/seller_frontend_service/util/validation_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using IgsWithBidsMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;

// Map fields from an AdScore proto to an Auction Result proto object.
// Will move fields from update_groups.
AuctionResult AdScoreToAuctionResult(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    std::optional<IgsWithBidsMap> maybe_bidding_groups,
    UpdateGroupMap& update_groups,
    const std::optional<AuctionResult::Error>& error,
    AuctionScope auction_scope, absl::string_view seller,
    const std::variant<ProtectedAudienceInput, ProtectedAuctionInput>&
        protected_auction_input,
    absl::string_view top_level_seller = "");

// Builds a ScoreAdsRequest using component_auction_results.
// This function moves the elements from component_auction_results and
// signals fields from auction_config and protected_auction_input.
// These fields should not be used after this function has been called.
std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
CreateTopLevelScoreAdsRawRequest(
    const SelectAdRequest::AuctionConfig& auction_config,
    std::variant<ProtectedAudienceInput, ProtectedAuctionInput>&
        protected_auction_input,
    std::vector<AuctionResult>& component_auction_results);

// Encodes, compresses and encrypts AdScore and bidding groups map
// as auction_result_ciphertext.
absl::StatusOr<std::string> CreateWinningAuctionResultCiphertext(
    const ScoreAdsResponse::AdScore& high_score,
    const std::optional<IgsWithBidsMap>& bidding_group_maps,
    const UpdateGroupMap& update_group_map, ClientType client_type,
    OhttpHpkeDecryptedMessage& decrypted_request,
    RequestLogContext& log_context);

// Encodes, compresses and encrypts client error
// as auction_result_ciphertext.
absl::StatusOr<std::string> CreateErrorAuctionResultCiphertext(
    const AuctionResult::Error& auction_error, ClientType client_type,
    OhttpHpkeDecryptedMessage& decrypted_request,
    RequestLogContext& log_context);

// Encodes, compresses and encrypts chaff response
// as auction_result_ciphertext.
absl::StatusOr<std::string> CreateChaffAuctionResultCiphertext(
    ClientType client_type, OhttpHpkeDecryptedMessage& decrypted_request,
    RequestLogContext& log_context);

// Collate all Igs that received bids from all server component auction results.
IgsWithBidsMap GetBuyerIgsWithBidsMap(
    std::vector<IgsWithBidsMap>& component_auction_bidding_groups);

// This function is used to decode an encrypted and compressed
// Server Component Auction Result.
absl::StatusOr<AuctionResult> UnpackageServerAuctionComponentResult(
    const SelectAdRequest::ComponentAuctionResult& component_auction_result,
    CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager);

// Decrypts Component Auction Result ciphertext and validate AuctionResult
// objects.
std::vector<AuctionResult> DecryptAndValidateComponentAuctionResults(
    const SelectAdRequest* request, absl::string_view seller_domain,
    absl::string_view request_generation_id,
    CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    ErrorAccumulator& error_accumulator, RequestLogContext& log_context);

template <typename T>
T AppProtectedAuctionInputDecodeHelper(absl::string_view encoded_data,
                                       ErrorAccumulator& error_accumulator) {
  T protected_auction_input;
  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(encoded_data);
  if (!decoded_request.ok()) {
    error_accumulator.ReportError(
        ErrorVisibility::CLIENT_VISIBLE,
        std::string(decoded_request.status().message()),
        ErrorCode::CLIENT_SIDE);
    return protected_auction_input;
  }

  std::string payload = std::move(decoded_request->compressed_data);
  if (!protected_auction_input.ParseFromArray(payload.data(), payload.size())) {
    error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE,
                                  kBadProtectedAudienceBinaryProto,
                                  ErrorCode::CLIENT_SIDE);
  }

  return protected_auction_input;
}

template <typename T>
T WebProtectedAuctionInputDecodeHelper(absl::string_view encoded_data,
                                       bool fail_fast,
                                       ErrorAccumulator& error_accumulator) {
  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(encoded_data);
  if (!decoded_request.ok()) {
    error_accumulator.ReportError(
        ErrorVisibility::CLIENT_VISIBLE,
        std::string(decoded_request.status().message()),
        ErrorCode::CLIENT_SIDE);
    return T{};
  }
  return Decode<T>(decoded_request->compressed_data, error_accumulator,
                   fail_fast);
}

ProtectedAudienceInput DecryptProtectedAudienceInput(
    absl::string_view encapsulated_req,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    ClientType client_type, ErrorAccumulator& error_accumulator);

ProtectedAuctionInput DecryptProtectedAuctionInput(
    absl::string_view encapsulated_req,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    ClientType client_type, ErrorAccumulator& error_accumulator);

// Gets the bidding groups after scoring is done.
google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
GetBiddingGroups(
    const BuyerBidsResponseMap& shared_buyer_bids_map,
    const absl::flat_hash_map<absl::string_view, BuyerInput>& buyer_inputs);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_PROTO_MAPPING_UTIL_H_
