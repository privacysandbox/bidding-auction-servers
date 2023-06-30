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

#include "services/seller_frontend_service/select_ad_reactor_app.h"

#include <utility>

#include "absl/strings/str_format.h"
#include "glog/logging.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/status_macros.h"
#include "services/seller_frontend_service/util/app_utils.h"
#include "src/cpp/communication/encoding_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

using BiddingGroupsMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;

SelectAdReactorForApp::SelectAdReactorForApp(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client, bool fail_fast)
    : SelectAdReactor(context, request, response, clients, config_client,
                      fail_fast) {}

absl::StatusOr<std::string> SelectAdReactorForApp::GetNonEncryptedResponse(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const BiddingGroupsMap& bidding_group_map,
    const std::optional<AuctionResult::Error>& error) {
  AuctionResult auction_result;
  if (error.has_value()) {
    *auction_result.mutable_error() = *error;
  } else if (high_score.has_value()) {
    auction_result.set_is_chaff(false);
    auction_result.set_bid(high_score->buyer_bid());
    auction_result.set_score(high_score->desirability());
    auction_result.set_interest_group_name(high_score->interest_group_name());
    auction_result.set_interest_group_owner(high_score->interest_group_owner());
    auction_result.set_ad_render_url(high_score->render());
    *auction_result.mutable_bidding_groups() = std::move(bidding_group_map);
    *auction_result.mutable_ad_component_render_urls() =
        high_score->component_renders();
  } else {
    auction_result.set_is_chaff(true);
  }

  // Serialized the data to bytes array.
  std::string serialized_result = auction_result.SerializeAsString();

  // Compress the bytes array before framing it with pre-amble and padding.
  absl::StatusOr<std::string> compressed_data = GzipCompress(serialized_result);
  if (!compressed_data.ok()) {
    std::string error = "Failed to compress the serialized response data\n";
    FinishWithInternalError(error);
    return absl::InternalError("");
  }

  return server_common::EncodeResponsePayload(
      server_common::CompressionType::kGzip, *compressed_data,
      GetEncodedDataSize(compressed_data->size()));
}

ProtectedAudienceInput SelectAdReactorForApp::GetDecodedProtectedAudienceInput(
    absl::string_view encoded_data) {
  ProtectedAudienceInput protected_audience_input;
  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(encoded_data);
  if (!decoded_request.ok()) {
    ReportError(ErrorVisibility::CLIENT_VISIBLE,
                std::string(decoded_request.status().message()),
                ErrorCode::CLIENT_SIDE);
    return protected_audience_input;
  }

  std::string payload = std::move(decoded_request->compressed_data);
  if (!protected_audience_input.ParseFromArray(payload.data(),
                                               payload.size())) {
    ReportError(ErrorVisibility::CLIENT_VISIBLE,
                kBadProtectedAudienceBinaryProto, ErrorCode::CLIENT_SIDE);
  }

  return protected_audience_input;
}

DecodedBuyerInputs SelectAdReactorForApp::GetDecodedBuyerinputs(
    const EncodedBuyerInputs& encoded_buyer_inputs) {
  DecodedBuyerInputs decoded_buyer_inputs;
  for (const auto& [owner, compressed_buyer_input] : encoded_buyer_inputs) {
    absl::StatusOr<std::string> decompressed_buyer_input =
        GzipDecompress(compressed_buyer_input);
    if (!decompressed_buyer_input.ok()) {
      ReportError(ErrorVisibility::CLIENT_VISIBLE,
                  absl::StrFormat(kBadCompressedBuyerInput, owner),
                  ErrorCode::CLIENT_SIDE);
      continue;
    }

    BuyerInput buyer_input;
    if (!buyer_input.ParseFromArray(decompressed_buyer_input->data(),
                                    decompressed_buyer_input->size())) {
      ReportError(ErrorVisibility::CLIENT_VISIBLE,
                  absl::StrFormat(kBadBuyerInputProto, owner),
                  ErrorCode::CLIENT_SIDE);
      continue;
    }

    decoded_buyer_inputs.insert({std::move(owner), std::move(buyer_input)});
  }

  return decoded_buyer_inputs;
}

}  // namespace privacy_sandbox::bidding_auction_servers
