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
#include "glog/logging.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/status_macros.h"
#include "services/seller_frontend_service/util/web_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

using BiddingGroupsMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;

SelectAdReactorForWeb::SelectAdReactorForWeb(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const SellerFrontEndConfig& config, bool fail_fast)
    : SelectAdReactor(context, request, response, clients, config, fail_fast) {}

absl::StatusOr<std::string> SelectAdReactorForWeb::GetNonEncryptedResponse(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const BiddingGroupsMap& bidding_group_map,
    const std::optional<AuctionResult::Error>& error) {
  auto error_handler =
      absl::bind_front(&SelectAdReactorForWeb::FinishWithInternalError, this);
  absl::StatusOr<std::vector<unsigned char>> encoded_data;
  PS_ASSIGN_OR_RETURN(encoded_data, Encode(high_score, bidding_group_map, error,
                                           error_handler));

  absl::string_view data_to_compress = absl::string_view(
      reinterpret_cast<char*>(encoded_data->data()), encoded_data->size());

  absl::StatusOr<std::string> compressed_data = GzipCompress(data_to_compress);
  if (!compressed_data.ok()) {
    LOG(ERROR) << "Failed to compress the CBOR serialized data: "
               << compressed_data.status().message();
    FinishWithInternalError("Failed to compress CBOR data");
    return absl::InternalError("");
  }

  // Pad data so that its size becomes the next smallest power of 2.
  compressed_data->resize(std::max(absl::bit_ceil(compressed_data->size()),
                                   kMinAuctionResultBytes));
  return std::move(*compressed_data);
}

ProtectedAudienceInput SelectAdReactorForWeb::GetDecodedProtectedAudienceInput(
    absl::string_view encoded_data) {
  return Decode(encoded_data, error_accumulator_, fail_fast_);
}

DecodedBuyerInputs SelectAdReactorForWeb::GetDecodedBuyerinputs(
    const EncodedBuyerInputs& encoded_buyer_inputs) {
  return DecodeBuyerInputs(encoded_buyer_inputs, error_accumulator_,
                           fail_fast_);
}

}  // namespace privacy_sandbox::bidding_auction_servers
