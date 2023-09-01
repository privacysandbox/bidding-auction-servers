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
#include "services/seller_frontend_service/util/framing_utils.h"
#include "src/cpp/communication/encoding_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

using BiddingGroupsMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;
using ReportErrorSignature = std::function<void(
    ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    const std::string& msg, ErrorCode error_code)>;

using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;

namespace {

template <typename T>
T GetDecodedProtectedAuctionInputHelper(absl::string_view encoded_data,
                                        bool fail_fast,
                                        ReportErrorSignature ReportError,
                                        ErrorAccumulator& error_accumulator) {
  T protected_auction_input;
  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(encoded_data);
  if (!decoded_request.ok()) {
    ReportError(ErrorVisibility::CLIENT_VISIBLE,
                std::string(decoded_request.status().message()),
                ErrorCode::CLIENT_SIDE);
    return protected_auction_input;
  }

  std::string payload = std::move(decoded_request->compressed_data);
  if (!protected_auction_input.ParseFromArray(payload.data(), payload.size())) {
    ReportError(ErrorVisibility::CLIENT_VISIBLE,
                kBadProtectedAudienceBinaryProto, ErrorCode::CLIENT_SIDE);
  }

  return protected_auction_input;
}

}  // namespace

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
    auction_result.mutable_win_reporting_urls()
        ->mutable_buyer_reporting_urls()
        ->set_reporting_url(high_score->win_reporting_urls()
                                .buyer_reporting_urls()
                                .reporting_url());
    for (auto& [event, url] : high_score->win_reporting_urls()
                                  .buyer_reporting_urls()
                                  .interaction_reporting_urls()) {
      auction_result.mutable_win_reporting_urls()
          ->mutable_buyer_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, url);
    }
    auction_result.mutable_win_reporting_urls()
        ->mutable_top_level_seller_reporting_urls()
        ->set_reporting_url(high_score->win_reporting_urls()
                                .top_level_seller_reporting_urls()
                                .reporting_url());
    for (auto& [event, url] : high_score->win_reporting_urls()
                                  .top_level_seller_reporting_urls()
                                  .interaction_reporting_urls()) {
      auction_result.mutable_win_reporting_urls()
          ->mutable_top_level_seller_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, url);
    }
    *auction_result.mutable_bidding_groups() = std::move(bidding_group_map);
    *auction_result.mutable_ad_component_render_urls() =
        high_score->component_renders();
    auction_result.set_ad_type(high_score->ad_type());
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
  return GetDecodedProtectedAuctionInputHelper<ProtectedAudienceInput>(
      encoded_data, fail_fast_,
      std::bind(&SelectAdReactorForApp::ReportError, this,
                std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3),
      error_accumulator_);
}

ProtectedAuctionInput SelectAdReactorForApp::GetDecodedProtectedAuctionInput(
    absl::string_view encoded_data) {
  return GetDecodedProtectedAuctionInputHelper<ProtectedAuctionInput>(
      encoded_data, fail_fast_,
      std::bind(&SelectAdReactorForApp::ReportError, this,
                std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3),
      error_accumulator_);
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

void SelectAdReactorForApp::MayPopulateProtectedAppSignalsBuyerInput(
    GetBidsRequest::GetBidsRawRequest* get_bids_raw_request) {
  if (!is_pas_enabled_) {
    VLOG(8) << "Protected app signals is not enabled and hence not populating "
               "PAS buyer input";
    // We don't want to forward the protected signals when feature is disabled,
    // even when if client sent them erroneously.
    get_bids_raw_request->mutable_buyer_input()->clear_protected_app_signals();
    return;
  }

  if (!get_bids_raw_request->buyer_input().has_protected_app_signals()) {
    VLOG(8) << "No protected app signals in buyer inputs from client";
    return;
  }

  VLOG(3) << "Found protected signals in buyer input, passing them to get bids";
  auto* protected_app_signals_buyer_input =
      get_bids_raw_request->mutable_protected_app_signals_buyer_input();
  protected_app_signals_buyer_input->mutable_protected_app_signals()->Swap(
      get_bids_raw_request->mutable_buyer_input()
          ->mutable_protected_app_signals());
  get_bids_raw_request->mutable_buyer_input()->clear_protected_app_signals();
}

std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
SelectAdReactorForApp::CreateGetBidsRequest(absl::string_view seller,
                                            const std::string& buyer_ig_owner,
                                            const BuyerInput& buyer_input) {
  auto request = SelectAdReactor::CreateGetBidsRequest(seller, buyer_ig_owner,
                                                       buyer_input);
  MayPopulateProtectedAppSignalsBuyerInput(request.get());
  return request;
}

std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
SelectAdReactorForApp::CreateScoreAdsRequest() {
  auto request = SelectAdReactor::CreateScoreAdsRequest();
  MayPopulateProtectedAppSignalsBids(request.get());
  return request;
}

ProtectedAppSignalsAdWithBidMetadata
SelectAdReactorForApp::BuildProtectedAppSignalsAdWithBidMetadata(
    absl::string_view buyer, const ProtectedAppSignalsAdWithBid& input) {
  ProtectedAppSignalsAdWithBidMetadata result;
  if (input.has_ad()) {
    *result.mutable_ad() = input.ad();
  }
  result.set_bid(input.bid());
  result.set_render(input.render());
  result.set_modeling_signals(input.modeling_signals());
  result.set_ad_cost(input.ad_cost());
  result.set_egress_features(input.egress_features());
  result.set_owner(buyer);
  return result;
}

void SelectAdReactorForApp::MayPopulateProtectedAppSignalsBids(
    ScoreAdsRequest::ScoreAdsRawRequest* score_ads_raw_request) {
  if (!is_pas_enabled_) {
    VLOG(8) << "Protected app signals is not enabled and hence not populating "
               "PAS bids";
    return;
  }

  VLOG(3) << "Protected App signals, may add protected app signals bids to "
             "score ads request";
  for (const auto& [buyer, get_bid_response] : shared_buyer_bids_map_) {
    for (int i = 0; i < get_bid_response->protected_app_signals_bids_size();
         i++) {
      auto ad_with_bid_metadata = BuildProtectedAppSignalsAdWithBidMetadata(
          buyer, get_bid_response->protected_app_signals_bids().at(i));
      score_ads_raw_request->mutable_protected_app_signals_ad_bids()->Add(
          std::move(ad_with_bid_metadata));
    }
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
