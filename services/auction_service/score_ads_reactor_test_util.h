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
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"

namespace privacy_sandbox::bidding_auction_servers {

using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;

constexpr char kKeyId[] = "keyid";
constexpr char kSecret[] = "secret";
constexpr float kTestBid = 1.25;
constexpr char kTestProtectedAppSignalsAdOwner[] = "https://PAS-Ad-Owner.com";
constexpr char kTestReportingWinResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"sellerLogs":["testLog"], "sellerErrors":["testLog"], "sellerWarnings":["testLog"],
"reportWinResponse":{"reportWinUrl":"http://reportWinUrl.com","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"buyerLogs":["testLog"], "buyerErrors":["testLog"], "buyerWarnings":["testLog"]})";
constexpr char kTestReportingResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"sellerLogs":["testLog"]})";
constexpr char kEmptyTestReportingResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"","sendReportToInvoked":false,"registerAdBeaconInvoked":false,"interactionReportingUrls":{}},"sellerLogs":[], "sellerErrors":[], "sellerWarnings":[])";
constexpr char kTestReportResultResponseJson[] =
    R"({"response":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","interactionReportingUrls":{"click":"http://event.com"}},"logs":["testLog"], "errors":["testLog"], "warnings":["testLog"]})";
constexpr char kTestTopLevelReportResultUrl[] = "http://reportResultUrl.com";

ProtectedAppSignalsAdWithBidMetadata GetProtectedAppSignalsAdWithBidMetadata(
    absl::string_view render_url, float bid = kTestBid);

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

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_TEST_UTIL_H_
