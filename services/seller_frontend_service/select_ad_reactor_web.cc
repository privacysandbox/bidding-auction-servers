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
    ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
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

// TODO(b/279955398): Consider using `cbor_describe`.
void LogResponse(const std::optional<ScoreAdsResponse::AdScore>& high_score,
                 const BiddingGroupsMap& bidding_group_map,
                 const std::optional<AuctionResult::Error>& error,
                 const ConsentedDebuggingLogger& consented_logger) {
  if (error.has_value()) {
    consented_logger.vlog(
        1, absl::StrCat("AuctionResult::Error: ", error->DebugString()));
  } else if (high_score.has_value()) {
    consented_logger.vlog(1,
                          absl::StrCat("AdScore: ", high_score->DebugString()));
    for (const auto& [buyer, ig] : bidding_group_map) {
      consented_logger.vlog(
          1, absl::StrCat("bidding_group[", buyer, "]: ", ig.DebugString()));
    }
  } else {
    consented_logger.vlog(1, "AuctionResult: is_chaff: true");
  }
}

}  // namespace

SelectAdReactorForWeb::SelectAdReactorForWeb(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client, bool fail_fast)
    : SelectAdReactor(context, request, response, clients, config_client,
                      fail_fast) {}

absl::StatusOr<std::string> SelectAdReactorForWeb::GetNonEncryptedResponse(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const BiddingGroupsMap& bidding_group_map,
    const std::optional<AuctionResult::Error>& error) {
  if (consented_logger_.has_value() && consented_logger_->IsConsented()) {
    LogResponse(high_score, bidding_group_map, error,
                consented_logger_.value());
  }

  auto error_handler =
      absl::bind_front(&SelectAdReactorForWeb::FinishWithInternalError, this);
  PS_ASSIGN_OR_RETURN(auto encoded_data, Encode(high_score, bidding_group_map,
                                                error, error_handler));

  absl::string_view data_to_compress = absl::string_view(
      reinterpret_cast<char*>(encoded_data.data()), encoded_data.size());

  absl::StatusOr<std::string> compressed_data = GzipCompress(data_to_compress);
  if (!compressed_data.ok()) {
    LOG(ERROR) << "Failed to compress the CBOR serialized data: "
               << compressed_data.status().message();
    FinishWithInternalError("Failed to compress CBOR data");
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
