// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/buyer_frontend_service/util/proto_factory.h"

namespace privacy_sandbox::bidding_auction_servers {
using GetBidsRawRequest = GetBidsRequest::GetBidsRawRequest;
using GetBidsRawResponse = GetBidsResponse::GetBidsRawResponse;
using GenerateBidsRawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using GenerateProtectedAppSignalsBidsRawRequest =
    GenerateProtectedAppSignalsBidsRequest::
        GenerateProtectedAppSignalsBidsRawRequest;

std::unique_ptr<GetBidsRawResponse> CreateGetBidsRawResponse(
    std::unique_ptr<GenerateBidsResponse::GenerateBidsRawResponse>
        raw_response) {
  auto get_bids_raw_response = std::make_unique<GetBidsRawResponse>();

  if (!raw_response->IsInitialized() || raw_response->bids_size() == 0) {
    // Initialize empty list.
    get_bids_raw_response->mutable_bids();
    return get_bids_raw_response;
  }

  get_bids_raw_response->mutable_bids()->Swap(raw_response->mutable_bids());
  return get_bids_raw_response;
}

// Copy properties from IG from device to IG for Bidding.
void CopyIGFromDeviceToIGForBidding(
    const BuyerInput::InterestGroup& ig_from_device,
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding*
        mutable_ig_for_bidding) {
  mutable_ig_for_bidding->set_name(ig_from_device.name());

  if (!ig_from_device.user_bidding_signals().empty()) {
    mutable_ig_for_bidding->set_user_bidding_signals(
        ig_from_device.user_bidding_signals());
  }

  if (!ig_from_device.ad_render_ids().empty()) {
    mutable_ig_for_bidding->mutable_ad_render_ids()->CopyFrom(
        ig_from_device.ad_render_ids());
  }
  if (!ig_from_device.component_ads().empty()) {
    mutable_ig_for_bidding->mutable_ad_component_render_ids()->CopyFrom(
        ig_from_device.component_ads());
  }

  if (!ig_from_device.bidding_signals_keys().empty()) {
    mutable_ig_for_bidding->mutable_trusted_bidding_signals_keys()->MergeFrom(
        ig_from_device.bidding_signals_keys());
  }

  // Set Device Signals.
  if (ig_from_device.has_browser_signals() &&
      ig_from_device.browser_signals().IsInitialized()) {
    mutable_ig_for_bidding->mutable_browser_signals()->CopyFrom(
        ig_from_device.browser_signals());
  } else if (ig_from_device.has_android_signals()) {
    mutable_ig_for_bidding->mutable_android_signals()->CopyFrom(
        ig_from_device.android_signals());
  }
}

std::unique_ptr<GenerateBidsRawRequest> CreateGenerateBidsRawRequest(
    const GetBidsRawRequest& get_bids_raw_request,
    const BuyerInput& buyer_input,
    std::unique_ptr<BiddingSignals> bidding_signals,
    const server_common::LogContext& log_context) {
  auto generate_bids_raw_request = std::make_unique<GenerateBidsRawRequest>();

  // 1. Set Interest Group for bidding
  for (int i = 0; i < buyer_input.interest_groups_size(); i++) {
    const auto& interest_group_from_device = buyer_input.interest_groups(i);
    // IG must have a name.
    if (interest_group_from_device.name().empty()) {
      continue;
    }
    // Add InterestGroupForBidding.
    auto mutable_interest_group_for_bidding =
        generate_bids_raw_request->mutable_interest_group_for_bidding()->Add();

    // Copy from IG from device.
    CopyIGFromDeviceToIGForBidding(interest_group_from_device,
                                   mutable_interest_group_for_bidding);
  }

  // 2. Set Auction Signals.
  generate_bids_raw_request->set_auction_signals(
      get_bids_raw_request.auction_signals());

  // 3. Set Buyer Signals.
  if (!get_bids_raw_request.buyer_signals().empty()) {
    generate_bids_raw_request->set_buyer_signals(
        get_bids_raw_request.buyer_signals());
  } else {
    generate_bids_raw_request->set_buyer_signals("");
  }

  // 4. Set Bidding Signals
  generate_bids_raw_request->set_allocated_bidding_signals(
      bidding_signals->trusted_signals.release());

  // 5. Set Debug Reporting Flag
  generate_bids_raw_request->set_enable_debug_reporting(
      get_bids_raw_request.enable_debug_reporting());

  generate_bids_raw_request->set_publisher_name(
      get_bids_raw_request.publisher_name());
  generate_bids_raw_request->set_seller(get_bids_raw_request.seller());
  generate_bids_raw_request->set_top_level_seller(
      get_bids_raw_request.top_level_seller());

  // 6. Set logging context.
  if (!log_context.adtech_debug_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_adtech_debug_id(
        log_context.adtech_debug_id());
  }
  if (!log_context.generation_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_generation_id(
        log_context.generation_id());
  }

  // 7. Set consented debug config.
  if (get_bids_raw_request.has_consented_debug_config()) {
    *generate_bids_raw_request->mutable_consented_debug_config() =
        get_bids_raw_request.consented_debug_config();
  }

  return generate_bids_raw_request;
}

std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>
CreateGenerateProtectedAppSignalsBidsRawRequest(
    const GetBidsRawRequest& raw_request) {
  auto generate_bids_raw_request =
      std::make_unique<GenerateProtectedAppSignalsBidsRawRequest>();
  generate_bids_raw_request->set_auction_signals(raw_request.auction_signals());

  if (!raw_request.buyer_signals().empty()) {
    generate_bids_raw_request->set_buyer_signals(raw_request.buyer_signals());
  } else {
    generate_bids_raw_request->set_buyer_signals("");
  }

  *generate_bids_raw_request->mutable_protected_app_signals() =
      raw_request.protected_app_signals_buyer_input().protected_app_signals();
  if (raw_request.protected_app_signals_buyer_input()
          .has_contextual_protected_app_signals_data()) {
    *generate_bids_raw_request
         ->mutable_contextual_protected_app_signals_data() =
        raw_request.protected_app_signals_buyer_input()
            .contextual_protected_app_signals_data();
  }

  generate_bids_raw_request->set_seller(raw_request.seller());

  generate_bids_raw_request->set_publisher_name(raw_request.publisher_name());

  const auto& log_context = raw_request.log_context();
  if (!log_context.adtech_debug_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_adtech_debug_id(
        log_context.adtech_debug_id());
  }
  if (!log_context.generation_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_generation_id(
        log_context.generation_id());
  }

  if (raw_request.has_consented_debug_config()) {
    *generate_bids_raw_request->mutable_consented_debug_config() =
        raw_request.consented_debug_config();
  }

  generate_bids_raw_request->set_enable_debug_reporting(
      raw_request.enable_debug_reporting());

  generate_bids_raw_request->set_enable_unlimited_egress(
      raw_request.enable_unlimited_egress());

  return generate_bids_raw_request;
}

}  // namespace privacy_sandbox::bidding_auction_servers
