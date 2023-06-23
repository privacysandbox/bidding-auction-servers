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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_

#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/seller_frontend_config.pb.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/util/app_utils.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kAuctionHost[] = "auction-server.com";
constexpr char kSellerOriginDomain[] = "seller.com";
constexpr double kAdCost = 1.0;
constexpr int kModelingSignals = 0;
constexpr int kDefaultNumAdComponents = 3;
constexpr absl::string_view kSampleInterestGroupName = "interest_group";
constexpr absl::string_view kEmptyBuyer = "";
constexpr absl::string_view kSampleBuyer = "ad_tech_A.com";
constexpr absl::string_view kSampleBuyer2 = "ad_tech_B.com";
constexpr absl::string_view kSampleBuyer3 = "ad_tech_C.com";
constexpr absl::string_view kSampleGenerationId = "a-standard-uuid";
constexpr absl::string_view kSampleSellerDebugId = "sample-seller-debug-id";
constexpr absl::string_view kSampleBuyerDebugId = "sample-buyer-debug-id";
constexpr absl::string_view kSampleBuyerSignals = "[]";
constexpr float kNonZeroBidValue = 1.0;
constexpr float kZeroBidValue = 0.0;
constexpr int kNumAdComponentRenderUrl = 1;
constexpr int kNonZeroDesirability = 1;

absl::flat_hash_map<std::string, std::string> BuildBuyerWinningAdUrlMap(
    const SelectAdRequest& request);

void SetupBuyerClientMock(
    absl::string_view hostname,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients,
    const std::optional<GetBidsResponse>& bid,
    bool repeated_get_allowed = false);

void BuildAdWithBidFromAdWithBidMetadata(
    const ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata& input,
    AdWithBid* result);

AdWithBid BuildNewAdWithBid(
    const std::string& ad_url,
    absl::optional<absl::string_view> interest_group = absl::nullopt,
    absl::optional<float> bid_value = absl::nullopt,
    const bool enable_event_level_debug_reporting = false,
    int number_ad_component_render_urls = kDefaultNumAdComponents);

void SetupScoringProviderMock(
    const MockAsyncProvider<BuyerBidsList, ScoringSignals>& provider,
    const BuyerBidsList& expected_buyer_bids,
    const std::optional<std::string>& ad_render_urls,
    bool repeated_get_allowed = false);

SellerFrontEndConfig CreateConfig();

SelectAdResponse RunRequest(const SellerFrontEndConfig& config,
                            const ClientRegistry& clients,
                            const SelectAdRequest& request);

server_common::PrivateKey GetPrivateKey();

SelectAdRequest GetSampleSelectAdRequest(
    SelectAdRequest::ClientType client_type,
    absl::string_view seller_origin_domain);

BuyerBidsList GetBuyerClientsAndBidsForReactor(
    const SelectAdRequest& request,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients);

std::pair<SelectAdRequest, ClientRegistry>
GetSelectAdRequestAndClientRegistryForTest(
    SelectAdRequest::ClientType client_type, std::optional<float> buyer_bid,
    const MockAsyncProvider<BuyerBidsList, ScoringSignals>&
        scoring_signals_provider,
    const ScoringAsyncClientMock& scoring_client,
    const BuyerFrontEndAsyncClientFactoryMock&
        buyer_front_end_async_client_factory_mock,
    server_common::MockKeyFetcherManager* mock_key_fetcher_manager,
    BuyerBidsList& expected_buyer_bids, absl::string_view seller_origin_domain);

std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetCborEncodedEncryptedInputAndOhttpContext(
    const ProtectedAudienceInput& protected_audience_input);

std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetProtoEncodedEncryptedInputAndOhttpContext(
    const ProtectedAudienceInput& protected_audience_input);

template <typename T>
SelectAdResponse RunReactorRequest(const SellerFrontEndConfig& config,
                                   const ClientRegistry& clients,
                                   const SelectAdRequest& request,
                                   bool fail_fast = false) {
  metric::SfeContextMap()->Get(&request);
  grpc::CallbackServerContext context;
  SelectAdResponse response;
  T reactor(&context, &request, &response, clients, config, fail_fast);
  reactor.Execute();
  return response;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_
