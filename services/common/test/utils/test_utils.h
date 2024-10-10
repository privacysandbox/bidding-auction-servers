/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_TEST_UTILS_TEST_UTILS_H_
#define SERVICES_COMMON_TEST_UTILS_TEST_UTILS_H_

#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kTestKeyId[] = "key_id";
constexpr char kTestSecret[] = "secret";
constexpr char kTestPublisherName[] = "test_publisher";
constexpr char kTestAuctionSignals[] = "test_auction_signals";
constexpr char kTestBuyerSignals[] = "test_buyer_signals";
constexpr char kTestSeller[] = "test_seller";
constexpr char kTestGenerationId[] = "test_generation_uuid";
constexpr char kTestAdTechDebugId[] = "test_ad_tech_debug_id";
constexpr char kTestConsentedDebuggingToken[] =
    "test_consented_debugging_token";
constexpr char kTestProtectedAppSignals[] = "test_protected_app_signals";
constexpr char kTestContextualPasAdRenderId[] = "ad1";
constexpr int kTestEncodingVersion = 5;
constexpr char kTestInterestGroupName[] = "test_ig";
constexpr int kTestBidValue1 = 10.0;
constexpr int kTestAdCost1 = 2.0;
constexpr int kTestModelingSignals1 = 54;
constexpr char kTestEgressPayload[] = "test_egress_payload";
constexpr char kTestTemporaryEgressPayload[] = "test_temporary_egress_payload";
constexpr char kTestRender1[] = "https://test-render.com";
constexpr char kTestMetadataKey1[] = "test_metadata_key";
constexpr char kTestAdComponent[] = "test_ad_component";
constexpr char kTestCurrency1[] = "USD";
constexpr int kTestMetadataValue1 = 12;
constexpr int kTestBidValue2 = 20.0;
constexpr int kTestAdCost2 = 4.0;
constexpr int kTestModelingSignals2 = 3;
constexpr char kTestRender2[] = "https://test-render-2.com";
constexpr char kTestMetadataKey2[] = "test_metadata_key_2";
constexpr char kTestCurrency2[] = "RS";
constexpr int kTestMetadataValue2 = 51;
constexpr int kTestMultiBidLimit = 3;

// Creates a GetBidsRawRequest using hardcoded test data.
GetBidsRequest::GetBidsRawRequest CreateGetBidsRawRequest(
    bool add_protected_signals_input = true,
    bool add_protected_audience_input = false);

// Creates a GetBids Request using hardcoded test data.
GetBidsRequest CreateGetBidsRequest(bool add_protected_signals_input = true,
                                    bool add_protected_audience_input = false);

// Creates an AdWithBid for testing purposes.
AdWithBid CreateAdWithBid();

// Creates an ProtectedAppSignalsAdWithBid for testing purposes.
ProtectedAppSignalsAdWithBid CreateProtectedAppSignalsAdWithBid();

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_UTILS_TEST_UTILS_H_
