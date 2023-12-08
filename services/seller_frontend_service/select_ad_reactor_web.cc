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
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/strings/str_format.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/cpp/communication/encoding_utils.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using BiddingGroupsMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;
using ReportErrorSignature = std::function<void(
    log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    const std::string& msg, ErrorCode error_code)>;

namespace {

template <typename T>
T GetDecodedProtectedAuctionInputHelper(absl::string_view encoded_data,
                                        bool fail_fast,
                                        ReportErrorSignature ReportError,
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
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client, bool fail_fast,
    int max_buyers_solicited)
    : SelectAdReactor(context, request, response, clients, config_client,
                      fail_fast, max_buyers_solicited) {}

BiddingGroupsMap SelectAdReactorForWeb::GetBiddingGroups() {
  BiddingGroupsMap bidding_groups;
  for (const auto& [buyer, ad_with_bids] : shared_buyer_bids_map_) {
    // Mapping from buyer to interest groups that are associated with non-zero
    // bids.
    absl::flat_hash_set<absl::string_view> buyer_interest_groups;
    for (const auto& ad_with_bid : ad_with_bids->bids()) {
      if (ad_with_bid.bid() > 0) {
        buyer_interest_groups.insert(ad_with_bid.interest_group_name());
      }
    }
    const auto& buyer_input = buyer_inputs_->at(buyer);
    AuctionResult::InterestGroupIndex ar_interest_group_index;
    int ig_index = 0;
    for (const auto& interest_group : buyer_input.interest_groups()) {
      // If the interest group name is one of the groups returned by the bidding
      // service then record its index.
      if (buyer_interest_groups.contains(interest_group.name())) {
        ar_interest_group_index.add_index(ig_index);
      }
      ig_index++;
    }
    bidding_groups.try_emplace(buyer, std::move(ar_interest_group_index));
  }
  return bidding_groups;
}

absl::StatusOr<std::string> SelectAdReactorForWeb::GetNonEncryptedResponse(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const std::optional<AuctionResult::Error>& error) {
  auto error_handler =
      absl::bind_front(&SelectAdReactorForWeb::FinishWithStatus, this);
  std::string encoded_data;
  if (auction_scope_ == AuctionScope::kDeviceComponentSeller) {
    PS_ASSIGN_OR_RETURN(
        encoded_data,
        EncodeComponent(request_->auction_config().top_level_seller(),
                        high_score, GetBiddingGroups(), error, error_handler));
  } else {
    PS_ASSIGN_OR_RETURN(encoded_data, Encode(high_score, GetBiddingGroups(),
                                             error, error_handler));
  }
  PS_VLOG(kPlain, log_context_)
      << "AuctionResult:\n"
      << ([&]() {
           auto result = CborDecodeAuctionResultToProto(encoded_data);
           return result.ok() ? result->DebugString()
                              : result.status().ToString();
         }());

  absl::string_view data_to_compress = absl::string_view(
      reinterpret_cast<char*>(encoded_data.data()), encoded_data.size());

  absl::StatusOr<std::string> compressed_data = GzipCompress(data_to_compress);
  if (!compressed_data.ok()) {
    ABSL_LOG(ERROR) << "Failed to compress the CBOR serialized data: "
                    << compressed_data.status().message();
    FinishWithStatus(
        grpc::Status(grpc::INTERNAL, "Failed to compress CBOR data"));
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

}  // namespace privacy_sandbox::bidding_auction_servers
