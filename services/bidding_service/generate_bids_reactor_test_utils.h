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

#ifndef SERVICES_BIDDING_SERVICE_GENERATE_BIDS_REACTOR_TEST_UTILS_H_
#define SERVICES_BIDDING_SERVICE_GENERATE_BIDS_REACTOR_TEST_UTILS_H_

#include <string>
#include <vector>

#include "api/bidding_auction_servers.pb.h"
#include "public/query/v2/get_values_v2.grpc.pb.h"
#include "public/query/v2/get_values_v2.pb.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"

namespace privacy_sandbox::bidding_auction_servers {

using GenerateProtectedAppSignalsBidsRawRequest =
    GenerateProtectedAppSignalsBidsRequest::
        GenerateProtectedAppSignalsBidsRawRequest;
using kv_server::v2::GetValuesResponse;

constexpr char kTestConsentToken[] = "testConsentToken";
constexpr char kTestAuctionSignals[] =
    R"json({"auction_signal": "test 1"})json";
constexpr char kTestBuyerSignals[] = R"json({"buyer_signal": "test 2"})json";
constexpr char kTestSeller[] = "https://www.example-ssp.com";
constexpr char kTestTopLevelSeller[] = "https://www.example-top-ssp.com";
constexpr char kTestPublisherName[] = "www.example-publisher.com";
constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
constexpr char kTestRenderUrl[] = "https://adTech.com/ad?id=123";
constexpr int kTestEncodingVersion = 2;
constexpr char kTestAppInstallSignals[] = "test_app_install_signals";
constexpr char kTestDecodedAppInstallSignals[] = "deadbead";
constexpr char kGenerateBidEntryFunction[] = "generateBidEntryFunction";
constexpr char kTestProtectedAppSignals[] = "test_protected_app_signals";
constexpr char kTestRetrievalData[] = "test_retrieval_data";
constexpr double kTestWinningBid = 1.25;
constexpr int kTestAdRetrievalTimeoutMs = 100000;
constexpr char kTestEgressPayload[] =
    R"JSON({\"features\": [{\"name\": \"boolean-feature\", \"value\": true}]})JSON";
constexpr char kTestTemporaryEgressPayload[] =
    R"JSON({\"features\": [{\"name\": \"boolean-feature\", \"value\": false}]})JSON";
constexpr char kTestEgressPayloadBiggerThan3Bytes[] = "deadbeef";
constexpr char kTestEgressPayloadBiggerThan23bits[] = "f01020";
constexpr char kTestAdsRetrievalAdsResponse[] = "Ads Data And Metadata";
constexpr char kTestBuyerReportingId[] = "testBuyerReportingId";
constexpr char kTestAdsRetrievalContextualEmbeddingsResponse[] = R"JSON(
{
    "contextualEmbeddings1": {
        "value": "Y29udGV4dHVhbCBlbWJlZGRpbmc="
    },
    "contextualEmbeddings2": {
        "value": "Y29udGV4dA=="
    }
}
)JSON";
constexpr char kTestDebugReportingUrls[] = R"JSON(
{
  "auction_debug_loss_url": "test.com/debugLoss",
  "auction_debug_win_url": "test.com/debugWin"
}
)JSON";

// Creates a test PrivateAggregationContribution object.
PrivateAggregateContribution CreateTestPAggContribution(
    EventType event_type, absl::string_view event_name);

// Sets up the provided mock so that it could be use to mock the encryption
// decryption required for TEE communication.
void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client);

// Creates a generate protected app signals bids raw request based on the
// provided parameters.
GenerateProtectedAppSignalsBidsRawRequest CreateRawProtectedAppSignalsRequest(
    const std::string& auction_signals, const std::string& buyer_signals,
    const ProtectedAppSignals& protected_app_signals, const std::string& seller,
    const std::string& publisher_name,
    absl::optional<ContextualProtectedAppSignalsData> contextual_pas_data =
        absl::nullopt,
    bool enable_unlimited_egress = false);

// Creates a generate protected app signals bids request using the provided
// raw request.
GenerateProtectedAppSignalsBidsRequest CreateProtectedAppSignalsRequest(
    const GenerateProtectedAppSignalsBidsRawRequest& raw_request);

// Creates protected app signals proto message based on the provided input
// parameters.
ProtectedAppSignals CreateProtectedAppSignals(
    const std::string& app_install_signals, int version);

// Creates a mock response from `prepareDataForAdRetrieval` UDF.
std::string CreatePrepareDataForAdsRetrievalResponse(
    absl::string_view protected_app_signals = kTestDecodedAppInstallSignals,
    absl::string_view protected_embeddings = kTestRetrievalData);

// Creates a mock response from `generateBid` UDF.
std::string CreateGenerateBidsUdfResponse(
    absl::string_view render = kTestRenderUrl, double bid = kTestWinningBid,
    absl::string_view egress_payload_string = kTestEgressPayload,
    absl::string_view debug_reporting_urls = kTestDebugReportingUrls,
    absl::string_view temporary_egress_payload_string =
        kTestTemporaryEgressPayload);

// Creates a mock response from ads retrieval service.
absl::StatusOr<kv_server::v2::GetValuesResponse>
CreateAdsRetrievalOrKvLookupResponse(
    absl::string_view ads = kTestAdsRetrievalAdsResponse);

// Mocks the request dispatch to Roma using the provided expected_json_response.
// Also verifies expectations on the input request using the expected fields
// passed to this method.
absl::Status MockRomaExecution(std::vector<DispatchRequest>& batch,
                               BatchDispatchDoneCallback batch_callback,
                               absl::string_view expected_method_name,
                               absl::string_view expected_request_version,
                               const std::string& expected_json_response);

// Sets up expectations for the batch requests for UDFs that are to be run in
// Roma for Protected App Signals workflow.
void SetupProtectedAppSignalsRomaExpectations(
    MockV8DispatchClient& dispatcher, int& num_roma_dispatches,
    const absl::optional<std::string>&
        prepare_data_for_ad_retrieval_udf_response = absl::nullopt,
    const absl::optional<std::string>& generate_bid_udf_response =
        absl::nullopt);

// Sets up expectations for the batch requests for UDFs that are to be run in
// Roma for Protected App Signals contextual ads workflow.
void SetupContextualProtectedAppSignalsRomaExpectations(
    MockV8DispatchClient& dispatcher, int& num_roma_dispatches,
    absl::optional<std::string> generate_bid_udf_response = absl::nullopt);

// Sets up expectations on the ad retrieval client mock.
void SetupAdRetrievalClientExpectations(
    KVAsyncClientMock& ad_retrieval_client,
    absl::optional<absl::StatusOr<GetValuesResponse>> ads_retrieval_response =
        absl::nullopt);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_GENERATE_BIDS_REACTOR_TEST_UTILS_H_
