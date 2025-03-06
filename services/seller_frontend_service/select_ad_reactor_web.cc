// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/seller_frontend_service/select_ad_reactor_web.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/hash_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/k_anon/k_anon_utils.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/proto_mapping_util.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::protobuf::RepeatedPtrField;
using BiddingGroupsMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs =
    absl::flat_hash_map<absl::string_view, BuyerInputForBidding>;
using ReportErrorSignature = std::function<void(
    log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    const std::string& msg, ErrorCode error_code)>;

namespace {

template <typename T>
T GetDecodedProtectedAuctionInputHelper(absl::string_view encoded_data,
                                        bool fail_fast,
                                        const ReportErrorSignature& ReportError,
                                        ErrorAccumulator& error_accumulator) {
  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(encoded_data);
  if (!decoded_request.ok()) {
    ReportError(ErrorVisibility::CLIENT_VISIBLE,
                std::string(decoded_request.status().message()),
                ErrorCode::CLIENT_SIDE);
    return T{};
  }
  std::string payload = std::move(decoded_request->compressed_data);
  return Decode<T>(payload, error_accumulator, fail_fast);
}

}  // namespace

SelectAdReactorForWeb::SelectAdReactorForWeb(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, server_common::Executor* executor,
    const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client,
    const ReportWinMap& report_win_map, bool enable_cancellation,
    bool enable_kanon, bool enable_buyer_private_aggregate_reporting,
    int per_adtech_paapi_contributions_limit, bool fail_fast,
    int max_buyers_solicited)
    : SelectAdReactor(context, request, response, executor, clients,
                      config_client, report_win_map, enable_cancellation,
                      enable_kanon, enable_buyer_private_aggregate_reporting,
                      per_adtech_paapi_contributions_limit, fail_fast,
                      max_buyers_solicited) {}

KAnonJoinCandidate SelectAdReactorForWeb::GetKAnonJoinCandidate(
    const ScoreAdsResponse::AdScore& score) {
  return privacy_sandbox::bidding_auction_servers::GetKAnonJoinCandidate(
      score, report_win_map_);
}

absl::StatusOr<std::string> SelectAdReactorForWeb::GetNonEncryptedResponse(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const std::optional<AuctionResult::Error>& error,
    const AdScores* ghost_winning_scores,
    int per_adtech_paapi_contributions_limit) {
  auto error_handler =
      absl::bind_front(&SelectAdReactorForWeb::FinishWithStatus, this);
  std::string encoded_data;
  const auto decode_lambda = [&encoded_data, this]() {
    PS_ASSIGN_OR_RETURN(auto result,
                        CborDecodeAuctionResultAndNonceToProto(encoded_data));
    log_context_.SetEventMessageField(result.first);
    return absl::Status(absl::StatusCode::kOk,
                        absl::StrCat(" with nonce: ", result.second,
                                     " exported in EventMessage if consented"));
  };

  const bool is_component_auction =
      auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
      auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER;
  std::unique_ptr<KAnonAuctionResultData> kanon_data = nullptr;
  if (enable_enforce_kanon_) {
    kanon_data =
        std::make_unique<KAnonAuctionResultData>(GetKAnonAuctionResultData(
            high_score, ghost_winning_scores, is_component_auction,
            absl::bind_front(&SelectAdReactorForWeb::GetKAnonJoinCandidate,
                             this),
            log_context_));
  }

  if (auction_scope_ ==
      AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER) {
    PS_ASSIGN_OR_RETURN(
        encoded_data,
        EncodeComponent(
            request_->auction_config().top_level_seller(), high_score,
            GetBiddingGroups(shared_buyer_bids_map_, *buyer_inputs_),
            shared_ig_updates_map_, adtech_origin_debug_urls_map_, error,
            error_handler, auction_config_.ad_auction_result_nonce(),
            std::move(kanon_data)));
    PS_VLOG(kPlain, log_context_) << "AuctionResult: " << (decode_lambda());
  } else if (auction_scope_ ==
             AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
    // If this is server component auction, serialize as proto.
    AuctionResult auction_result;
    if (high_score) {
      auction_result = AdScoreToAuctionResult(
          high_score, GetBiddingGroups(shared_buyer_bids_map_, *buyer_inputs_),
          shared_ig_updates_map_, error, auction_scope_,
          request_->auction_config().seller(), protected_auction_input_,
          request_->auction_config().top_level_seller(), std::move(kanon_data));
    } else {
      auction_result = AdScoreToAuctionResult(
          high_score, std::nullopt, shared_ig_updates_map_, error,
          auction_scope_, request_->auction_config().seller(),
          protected_auction_input_, /*top_level_seller=*/"",
          std::move(kanon_data));
    }
    // Serialized the data to bytes array.
    encoded_data = auction_result.SerializeAsString();

    if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
      PS_VLOG(kPlain, log_context_)
          << "AuctionResult exported in EventMessage if consented";
      log_context_.SetEventMessageField(auction_result);
    }
  } else {
    // SINGLE_SELLER or SERVER_TOP_LEVEL Auction
    PS_ASSIGN_OR_RETURN(
        encoded_data,
        Encode(high_score,
               GetBiddingGroups(shared_buyer_bids_map_, *buyer_inputs_),
               shared_ig_updates_map_, error, error_handler,
               per_adtech_paapi_contributions_limit,
               auction_config_.ad_auction_result_nonce(),
               std::move(kanon_data)));
    PS_VLOG(kPlain, log_context_) << "AuctionResult:\n" << (decode_lambda());
  }

  absl::string_view data_to_compress = absl::string_view(
      reinterpret_cast<char*>(encoded_data.data()), encoded_data.size());

  absl::StatusOr<std::string> compressed_data = GzipCompress(data_to_compress);
  if (!compressed_data.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to compress the CBOR serialized data: "
        << compressed_data.status().message();
    return absl::InternalError("");
  }

  return server_common::EncodeResponsePayload(
      server_common::CompressionType::kGzip, *compressed_data,
      GetEncodedDataSize(compressed_data->size()));
}

ProtectedAudienceInput SelectAdReactorForWeb::GetDecodedProtectedAudienceInput(
    absl::string_view encoded_data) {
  return GetDecodedProtectedAuctionInputHelper<ProtectedAudienceInput>(
      encoded_data, fail_fast_,
      std::bind(&SelectAdReactorForWeb::ReportError, this,
                std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3),
      error_accumulator_);
}

ProtectedAuctionInput SelectAdReactorForWeb::GetDecodedProtectedAuctionInput(
    absl::string_view encoded_data) {
  return GetDecodedProtectedAuctionInputHelper<ProtectedAuctionInput>(
      encoded_data, fail_fast_,
      std::bind(&SelectAdReactorForWeb::ReportError, this,
                std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3),
      error_accumulator_);
}

DecodedBuyerInputs SelectAdReactorForWeb::GetDecodedBuyerinputs(
    const EncodedBuyerInputs& encoded_buyer_inputs) {
  return DecodeBuyerInputs(encoded_buyer_inputs, error_accumulator_,
                           fail_fast_);
}

absl::string_view SelectAdReactorForWeb::GetKAnonSetType() { return kFledge; }

}  // namespace privacy_sandbox::bidding_auction_servers
