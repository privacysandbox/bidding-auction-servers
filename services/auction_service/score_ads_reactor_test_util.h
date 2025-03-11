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

#ifndef SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_TEST_UTIL_H_
#define SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_TEST_UTIL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {

using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;

struct ScoreAdsRawRequestOptions {
  absl::string_view seller_signals = kTestSellerSignals;
  absl::string_view auction_signals = kTestAuctionSignals;
  absl::string_view scoring_signals = kTestScoringSignals;
  absl::string_view publisher_hostname = kTestPublisherHostName;
  const bool enable_debug_reporting = false;
  const bool enable_adtech_code_logging = false;
  absl::string_view top_level_seller = "";
  absl::string_view seller_currency = "";
  const uint32_t seller_data_version = kSellerDataVersion;
};

ScoreAdsRequest::ScoreAdsRawRequest BuildRawRequest(
    std::vector<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>
        ads_with_bids_to_add,
    const ScoreAdsRawRequestOptions& options = ScoreAdsRawRequestOptions{});

ScoreAdsRequest::ScoreAdsRawRequest BuildProtectedAppSignalsRawRequest(
    std::vector<ScoreAdsRequest::ScoreAdsRawRequest::
                    ProtectedAppSignalsAdWithBidMetadata>
        ads_with_bids_to_add,
    const ScoreAdsRawRequestOptions& options = ScoreAdsRawRequestOptions{});

struct AdWithBidMetadataParams {
  absl::string_view render_url = kTestRenderUrl;
  const float bid = kTestBid;
  absl::string_view interest_group_name = kTestInterestGroupName;
  absl::string_view interest_group_owner = kTestInterestGroupOwner;
  absl::string_view interest_group_origin = kInterestGroupOrigin;
  const double ad_cost = kTestAdCost;
  const uint32_t data_version = kTestDataVersion;
  std::optional<absl::string_view> buyer_reporting_id;
  std::optional<absl::string_view> buyer_and_seller_reporting_id;
  std::optional<absl::string_view> selected_buyer_and_seller_reporting_id;
  const int number_of_component_ads = kTestNumOfComponentAds;
  const bool k_anon_status = false;
  absl::string_view metadata_key = kTestMetadataKey;
  const int metadata_value = kTestAdMetadataValue;
  absl::string_view ad_component_render_url_base =
      kTestAdComponentRenderUrlBase;
  absl::string_view bid_currency = "";
  const bool make_metadata = true;
};

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
BuildTestAdWithBidMetadata(
    const AdWithBidMetadataParams& params = AdWithBidMetadataParams{});

ProtectedAppSignalsAdWithBidMetadata GetProtectedAppSignalsAdWithBidMetadata(
    absl::string_view render_url, float bid = kTestBid);

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
GetTestAdWithBidBarbecueWithComponents();

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata GetTestAdWithBidBar(
    absl::string_view buyer_reporting_id = "");

void PopulateTestAdWithBidMetdata(
    const PostAuctionSignals& post_auction_signals,
    const BuyerReportingDispatchRequestData& buyer_dispatch_data,
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata&
        ad_with_bid_metadata);

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
GetTestAdWithBidBarbecue();

void SetupTelemetryCheck(const ScoreAdsRequest& request);

class ScoreAdsReactorTestHelper {
 public:
  ScoreAdsReactorTestHelper();

  ScoreAdsResponse ExecuteScoreAds(
      const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
      MockV8DispatchClient& dispatcher,
      const AuctionServiceRuntimeConfig& runtime_config =
          AuctionServiceRuntimeConfig());
  ScoreAdsRequest request_;
  TrustedServersConfigClient config_client_{{}};
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  MockCryptoClientWrapper crypto_client_;
  std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger_ =
      std::make_unique<ScoreAdsNoOpLogger>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
};

absl::Status FakeExecute(std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback,
                         std::vector<std::string> json,
                         const bool call_wrapper_method = false,
                         const bool enable_adtech_code_logging = false);

absl::Status FakeExecute(
    std::vector<DispatchRequest>& batch,
    BatchDispatchDoneCallback done_callback,
    absl::flat_hash_map</*id=*/std::string, /*json_score*/ std::string>
        json_ad_scores,
    const bool call_wrapper_method = false,
    const bool enable_adtech_code_logging = false);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_TEST_UTIL_H_
