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

#include "services/common/test/utils/test_utils.h"

#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {

GetBidsRequest::GetBidsRawRequest CreateGetBidsRawRequest(
    bool add_protected_signals_input, bool add_protected_audience_input) {
  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_auction_signals(kTestAuctionSignals);
  raw_request.set_buyer_signals(kTestBuyerSignals);
  raw_request.set_publisher_name(kTestPublisherName);
  raw_request.set_seller(kTestSeller);
  raw_request.set_enable_debug_reporting(true);
  raw_request.set_enable_unlimited_egress(true);
  raw_request.set_enforce_kanon(true);
  raw_request.set_multi_bid_limit(kTestMultiBidLimit);
  raw_request.mutable_log_context()->set_generation_id(kTestGenerationId);
  raw_request.mutable_log_context()->set_adtech_debug_id(kTestAdTechDebugId);
  raw_request.mutable_consented_debug_config()->set_is_consented(true);
  raw_request.mutable_consented_debug_config()->set_token(
      kTestConsentedDebuggingToken);
  if (add_protected_signals_input) {
    auto* protected_app_signals =
        raw_request.mutable_protected_app_signals_buyer_input()
            ->mutable_protected_app_signals();
    protected_app_signals->set_app_install_signals(kTestProtectedAppSignals);
    protected_app_signals->set_encoding_version(kTestEncodingVersion);
    auto* contextual_protected_app_signals_data =
        raw_request.mutable_protected_app_signals_buyer_input()
            ->mutable_contextual_protected_app_signals_data();
    *contextual_protected_app_signals_data->mutable_ad_render_ids()->Add() =
        kTestContextualPasAdRenderId;
  }
  if (add_protected_audience_input) {
    *raw_request.mutable_buyer_input() = MakeARandomBuyerInput();
    auto interest_group =
        raw_request.mutable_buyer_input()->mutable_interest_groups()->Add();
    interest_group->set_name("ig_name");
    interest_group->add_bidding_signals_keys("key");
  }
  return raw_request;
}

GetBidsRequest CreateGetBidsRequest(bool add_protected_signals_input,
                                    bool add_protected_audience_input) {
  GetBidsRequest get_bids_request;
  auto raw_request = CreateGetBidsRawRequest(add_protected_signals_input,
                                             add_protected_audience_input);
  get_bids_request.set_request_ciphertext(raw_request.SerializeAsString());
  get_bids_request.set_key_id(kTestKeyId);
  return get_bids_request;
}

AdWithBid CreateAdWithBid() {
  AdWithBid ad_with_bid;
  ad_with_bid.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(kTestRender1, kTestMetadataKey1, kTestMetadataValue1));
  ad_with_bid.set_interest_group_name(kTestInterestGroupName);
  ad_with_bid.set_render(kTestRender1);
  ad_with_bid.set_bid(kTestBidValue1);
  ad_with_bid.add_ad_components(kTestAdComponent);
  ad_with_bid.set_bid_currency(kTestCurrency1);
  ad_with_bid.set_ad_cost(kTestAdCost1);
  ad_with_bid.set_modeling_signals(kTestModelingSignals1);
  return ad_with_bid;
}

ProtectedAppSignalsAdWithBid CreateProtectedAppSignalsAdWithBid() {
  ProtectedAppSignalsAdWithBid ad_with_bid;
  ad_with_bid.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(kTestRender2, kTestMetadataKey2, kTestMetadataValue2));
  ad_with_bid.set_bid(kTestBidValue2);
  ad_with_bid.set_render(kTestRender2);
  ad_with_bid.set_modeling_signals(kTestModelingSignals2);
  ad_with_bid.set_ad_cost(kTestAdCost2);
  ad_with_bid.set_bid_currency(kTestCurrency2);
  ad_with_bid.set_egress_payload(kTestEgressPayload);
  ad_with_bid.set_temporary_unlimited_egress_payload(
      kTestTemporaryEgressPayload);
  return ad_with_bid;
}

}  // namespace privacy_sandbox::bidding_auction_servers
