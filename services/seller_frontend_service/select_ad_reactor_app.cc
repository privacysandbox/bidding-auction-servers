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

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/hash_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/buyer_input_proto_utils.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/proto_mapping_util.h"
#include "src/communication/encoding_utils.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using BiddingGroupsMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs =
    absl::flat_hash_map<absl::string_view, BuyerInputForBidding>;
using ReportErrorSignature = std::function<void(
    log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    const std::string& msg, ErrorCode error_code)>;

using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;

namespace {

template <typename T>
T GetDecodedProtectedAuctionInputHelper(absl::string_view encoded_data,
                                        bool fail_fast,
                                        const ReportErrorSignature& ReportError,
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
    const TrustedServersConfigClient& config_client,
    const ReportWinMap& report_win_map, bool enable_cancellation,
    bool enable_kanon, bool enable_buyer_private_aggregate_reporting,
    int per_adtech_paapi_contributions_limit, bool fail_fast)
    : SelectAdReactor(context, request, response, clients, config_client,
                      report_win_map, enable_cancellation, enable_kanon,
                      fail_fast) {}

absl::StatusOr<std::string> SelectAdReactorForApp::GetNonEncryptedResponse(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const std::optional<AuctionResult::Error>& error,
    const AdScores* ghost_winning_scores,
    int per_adtech_paapi_contributions_limit) {
  AuctionResult auction_result;
  if (high_score) {
    auction_result = AdScoreToAuctionResult(
        high_score, GetBiddingGroups(shared_buyer_bids_map_, *buyer_inputs_),
        shared_ig_updates_map_, error, auction_scope_,
        request_->auction_config().seller(), protected_auction_input_,
        request_->auction_config().top_level_seller());
  } else {
    auction_result = AdScoreToAuctionResult(
        high_score, /*maybe_bidding_groups=*/std::nullopt,
        shared_ig_updates_map_, error, auction_scope_,
        request_->auction_config().seller(), protected_auction_input_,
        request_->auction_config().top_level_seller());
  }
  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    PS_VLOG(kPlain, log_context_)
        << "AuctionResult exported in EventMessage if consented";
    log_context_.SetEventMessageField(auction_result);
  }

  // Serialized the data to bytes array.
  std::string serialized_result = auction_result.SerializeAsString();

  // Compress the bytes array before framing it with pre-amble and padding.
  absl::StatusOr<std::string> compressed_data = GzipCompress(serialized_result);
  if (!compressed_data.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to compress the serialized response data: "
        << compressed_data.status().message();
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

    BuyerInputForBidding buyer_input_for_bidding =
        ToBuyerInputForBidding(buyer_input);
    decoded_buyer_inputs.insert({owner, std::move(buyer_input_for_bidding)});
  }

  return decoded_buyer_inputs;
}

void SelectAdReactorForApp::MayPopulateProtectedAppSignalsBuyerInput(
    absl::string_view buyer,
    GetBidsRequest::GetBidsRawRequest* get_bids_raw_request) {
  if (!is_pas_enabled_) {
    PS_VLOG(8, log_context_) << "Protected app signals is not enabled and "
                                "hence not populating PAS buyer input";
    // We don't want to forward the protected signals when feature is disabled,
    // even if client sent them erroneously.
    if (get_bids_raw_request->has_buyer_input()) {
      get_bids_raw_request->mutable_buyer_input()
          ->clear_protected_app_signals();
    }

    if (get_bids_raw_request->has_buyer_input_for_bidding()) {
      get_bids_raw_request->mutable_buyer_input_for_bidding()
          ->clear_protected_app_signals();
    }
    return;
  }

  if (!get_bids_raw_request->buyer_input().has_protected_app_signals() ||
      get_bids_raw_request->buyer_input()
          .protected_app_signals()
          .app_install_signals()
          .empty()) {
    PS_VLOG(8, log_context_)
        << "No protected app signals in buyer inputs from client";
    if (get_bids_raw_request->has_buyer_input()) {
      get_bids_raw_request->mutable_buyer_input()
          ->clear_protected_app_signals();
    }

    return;
  }

  if (!get_bids_raw_request->buyer_input_for_bidding()
           .has_protected_app_signals() ||
      get_bids_raw_request->buyer_input_for_bidding()
          .protected_app_signals()
          .app_install_signals()
          .empty()) {
    PS_VLOG(8, log_context_)
        << "No protected app signals in buyer inputs from client";

    if (get_bids_raw_request->has_buyer_input_for_bidding()) {
      get_bids_raw_request->mutable_buyer_input_for_bidding()
          ->clear_protected_app_signals();
    }
    return;
  }

  PS_VLOG(kNoisyInfo, log_context_)
      << "Found protected signals in buyer input, passing them to get bids";
  auto* protected_app_signals_buyer_input =
      get_bids_raw_request->mutable_protected_app_signals_buyer_input();
  protected_app_signals_buyer_input->mutable_protected_app_signals()->Swap(
      get_bids_raw_request->mutable_buyer_input_for_bidding()
          ->mutable_protected_app_signals());
  get_bids_raw_request->mutable_buyer_input()->clear_protected_app_signals();
  get_bids_raw_request->mutable_buyer_input_for_bidding()
      ->clear_protected_app_signals();

  // Add contextual Protected App Signals data to PAS buyer input.
  auto& per_buyer_config = request_->auction_config().per_buyer_config();
  auto buyer_config_it = per_buyer_config.find(buyer);
  if (buyer_config_it == per_buyer_config.end()) {
    PS_VLOG(kNoisyInfo, log_context_) << "No buyer config found for: " << buyer;
    return;
  }

  const auto& buyer_config = buyer_config_it->second;
  if (!buyer_config.has_contextual_protected_app_signals_data() ||
      buyer_config.contextual_protected_app_signals_data()
              .ad_render_ids_size() == 0) {
    PS_VLOG(kNoisyInfo, log_context_)
        << "No PAS ad render ids received via contextual "
           "path for buyer: "
        << buyer;
    return;
  }

  *protected_app_signals_buyer_input
       ->mutable_contextual_protected_app_signals_data() =
      buyer_config.contextual_protected_app_signals_data();
}

std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
SelectAdReactorForApp::CreateGetBidsRequest(
    const std::string& buyer_ig_owner,
    const BuyerInputForBidding& buyer_input) {
  auto request =
      SelectAdReactor::CreateGetBidsRequest(buyer_ig_owner, buyer_input);
  // Debug reporting is not supported for Android.
  request->set_enable_debug_reporting(false);
  // TODO(b/369159855): use is_debug_eligible from client.
  request->set_is_debug_eligible(request->enable_unlimited_egress());
  MayPopulateProtectedAppSignalsBuyerInput(buyer_ig_owner, request.get());
  return request;
}

std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
SelectAdReactorForApp::CreateScoreAdsRequest() {
  auto request = SelectAdReactor::CreateScoreAdsRequest();
  // Debug reporting is not supported for Android.
  request->set_enable_debug_reporting(false);
  std::visit(
      [&request, this](const auto& protected_auction_input) {
        if (enable_enforce_kanon_) {
          request->set_num_allowed_ghost_winners(
              protected_auction_input.num_k_anon_ghost_winners());
        }
      },
      protected_auction_input_);
  MayPopulateProtectedAppSignalsBids(request.get());
  return request;
}

ProtectedAppSignalsAdWithBidMetadata
SelectAdReactorForApp::BuildProtectedAppSignalsAdWithBidMetadata(
    absl::string_view buyer_owner, const ProtectedAppSignalsAdWithBid& input,
    bool k_anon_status) {
  ProtectedAppSignalsAdWithBidMetadata result;
  if (input.has_ad()) {
    *result.mutable_ad() = input.ad();
  }
  result.set_bid(input.bid());
  result.set_render(input.render());
  result.set_ad_cost(input.ad_cost());
  result.set_owner(buyer_owner);
  result.set_bid_currency(input.bid_currency());
  result.set_egress_payload(input.egress_payload());
  result.set_temporary_unlimited_egress_payload(
      input.temporary_unlimited_egress_payload());
  if (enable_enforce_kanon_) {
    result.set_k_anon_status(k_anon_status);
  }
  return result;
}

void SelectAdReactorForApp::MayPopulateProtectedAppSignalsBids(
    ScoreAdsRequest::ScoreAdsRawRequest* score_ads_raw_request) {
  if (!is_pas_enabled_) {
    PS_VLOG(8, log_context_) << "Protected app signals is not enabled and "
                                "hence not populating PAS bids";
    return;
  }

  PS_VLOG(kNoisyInfo, log_context_)
      << "Protected App signals, may add protected app "
         "signals bids to score ads request";
  for (const auto& [buyer_owner, get_bid_response] : shared_buyer_bids_map_) {
    for (const auto& ad_with_bid :
         get_bid_response->protected_app_signals_bids()) {
      auto ad_with_bid_metadata = BuildProtectedAppSignalsAdWithBidMetadata(
          buyer_owner, ad_with_bid, bid_k_anon_status_[&ad_with_bid]);
      score_ads_raw_request->mutable_protected_app_signals_ad_bids()->Add(
          std::move(ad_with_bid_metadata));
    }
  }
}

KAnonJoinCandidate SelectAdReactorForApp::GetKAnonJoinCandidate(
    const ScoreAdsResponse::AdScore& score) {
  HashUtil hash_util = HashUtil();
  KAnonJoinCandidate join_candidate;
  auto bidding_js_it = report_win_map_.buyer_report_win_js_urls.find(
      score.interest_group_owner());
  DCHECK(bidding_js_it != report_win_map_.buyer_report_win_js_urls.end());
  join_candidate.set_ad_render_url_hash(hash_util.HashedKAnonKeyForAdRenderURL(
      score.interest_group_owner(), bidding_js_it->second, score.render()));
  return join_candidate;
}

SelectAdReactor::BidKAnonHashSets
SelectAdReactorForApp::GetKAnonHashesForBids() {
  PS_VLOG(6, log_context_) << " " << __func__;
  SelectAdReactor::BidKAnonHashSets bid_k_anon_hashes =
      SelectAdReactor::GetKAnonHashesForBids();
  const auto& buyer_report_win_js_urls =
      report_win_map_.buyer_report_win_js_urls;
  HashUtil k_anon_hash_util;
  for (auto& [owner, get_bids_raw_response] : shared_buyer_bids_map_) {
    if (get_bids_raw_response->protected_app_signals_bids().empty()) {
      continue;
    }

    auto report_win_it = buyer_report_win_js_urls.find(owner);
    if (report_win_it == buyer_report_win_js_urls.end()) {
      PS_VLOG(5, log_context_)
          << "Unable to find buyer owner in win "
          << "reporting URLs, considering related hashes as "
          << "non-k-anonymous for buyer: " << owner;
      continue;
    }

    for (const auto& bid :
         get_bids_raw_response->protected_app_signals_bids()) {
      absl::flat_hash_set<std::string> k_anon_hashes_for_bid;
      k_anon_hashes_for_bid.insert(
          k_anon_hash_util.HashedKAnonKeyForAdRenderURL(
              owner, report_win_it->second, bid.render()));

      bid_k_anon_hashes[&bid] = std::move(k_anon_hashes_for_bid);
    }
  }
  return bid_k_anon_hashes;
}

absl::string_view SelectAdReactorForApp::GetKAnonSetType() { return kAndroid; }

}  // namespace privacy_sandbox::bidding_auction_servers
