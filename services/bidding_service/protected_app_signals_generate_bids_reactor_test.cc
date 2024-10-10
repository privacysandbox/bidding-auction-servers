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

#include "services/bidding_service/protected_app_signals_generate_bids_reactor.h"

#include <memory>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "include/grpcpp/server_context.h"
#include "public/query/v2/get_values_v2.pb.h"
#include "services/bidding_service/base_generate_bids_reactor.h"
#include "services/bidding_service/constants.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/bidding_service/generate_bids_reactor_test_utils.h"
#include "services/common/clients/kv_server/kv_async_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/feature_flags.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/json_util.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr int kPreparedRetrievalDataIndex = 0;
constexpr int kContextualSignalsIndex = 2;
constexpr int kAdRenderIdsIndex = 0;
constexpr int kNumAdRetrievalUdfArguments = 4;
constexpr int kNumKVLookupUdfArguments = 1;
constexpr char kTestAdRenderId[] = "TestAdId";
constexpr absl::string_view kTestConsentToken = "testConsentToken";

using Request = GenerateProtectedAppSignalsBidsRequest;
using RawRequest = GenerateProtectedAppSignalsBidsRequest::
    GenerateProtectedAppSignalsBidsRawRequest;
using Response = GenerateProtectedAppSignalsBidsResponse;
using RawResponse = GenerateProtectedAppSignalsBidsResponse::
    GenerateProtectedAppSignalsBidsRawResponse;
using KVLookUpResult =
    absl::StatusOr<std::unique_ptr<kv_server::v2::GetValuesResponse>>;
using kv_server::v2::GetValuesRequest;
using kv_server::v2::GetValuesResponse;
using kv_server::v2::ObliviousGetValuesRequest;

constexpr absl::string_view kLimitedEgressSchema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "boolean-feature"
        },
        {
          "name": "unsigned-integer-feature",
          "size": 7
        },
        {
          "name": "histogram-feature",
          "size": 1,
          "value": [
            {
              "name": "signed-integer-feature",
              "size": 1
            }
          ]
        }
      ]
    }
  )JSON";

constexpr absl::string_view kUnlimitedEgressSchema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "unsigned-integer-feature",
          "size": 7
        },
        {
          "name": "boolean-feature"
        },
        {
          "name": "histogram-feature",
          "size": 1,
          "value": [
            {
              "name": "signed-integer-feature",
              "size": 1
            }
          ]
        }
      ]
    }
  )JSON";

class GenerateBidsReactorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();

    TrustedServersConfigClient config_client({});
    config_client.SetOverride(kTrue, TEST_MODE);
    config_client.SetOverride(kTrue, ENABLE_PROTECTED_APP_SIGNALS);
    server_common::log::SetGlobalPSVLogLevel(20);

    key_fetcher_manager_ =
        CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
    SetupMockCryptoClientWrapper(crypto_client_);
    egress_schema_cache_ = PopulateEgressSchemaCache(kUnlimitedEgressSchema);
    limited_egress_schema_cache_ =
        PopulateEgressSchemaCache(kLimitedEgressSchema);
  }

  std::unique_ptr<EgressSchemaCache> PopulateEgressSchemaCache(
      absl::string_view egress_schema) {
    std::unique_ptr<CddlSpecCache> cddl_spec_cache =
        std::make_unique<CddlSpecCache>(
            "services/bidding_service/egress_cddl_spec/");
    CHECK_OK(cddl_spec_cache->Init());
    auto egress_schema_cache =
        std::make_unique<EgressSchemaCache>(std::move(cddl_spec_cache));
    CHECK_OK(egress_schema_cache->Update(egress_schema));
    return egress_schema_cache;
  }

  RawResponse RunReactorWithRequest(const RawRequest& raw_request,
                                    std::optional<BiddingServiceRuntimeConfig>
                                        runtime_config = std::nullopt) {
    if (!runtime_config.has_value()) {
      runtime_config = {
          .enable_buyer_debug_url_generation = false,
      };
    }
    // Create a request.
    auto request = CreateProtectedAppSignalsRequest(raw_request);
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<::google::protobuf::Message>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request);
    server_common::log::ServerToken(kTestConsentToken);

    // Run the request through the reactor and return the response.
    Response response;
    ProtectedAppSignalsGenerateBidsReactor reactor(
        &context_, dispatcher_, *runtime_config, &request, &response,
        key_fetcher_manager_.get(), &crypto_client_, &ad_retrieval_client_,
        &kv_async_client_, egress_schema_cache_.get(),
        limited_egress_schema_cache_.get());
    reactor.Execute();
    RawResponse raw_response;
    raw_response.ParseFromString(response.response_ciphertext());
    return raw_response;
  }

  Request request_;
  grpc::CallbackServerContext context_;
  MockCryptoClientWrapper crypto_client_;
  MockV8DispatchClient dispatcher_;
  KVAsyncClientMock ad_retrieval_client_;
  KVAsyncClientMock kv_async_client_;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<EgressSchemaCache> egress_schema_cache_;
  std::unique_ptr<EgressSchemaCache> limited_egress_schema_cache_;
};

TEST_F(GenerateBidsReactorTest, WinningBidIsGenerated) {
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(dispatcher_, num_roma_dispatches);

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);

  // Verify the bid returned by the generateBid UDF is the same returned by
  // the reactor.
  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);
}

TEST_F(GenerateBidsReactorTest, AdsRetrievalTimeoutIsUsed) {
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(dispatcher_, num_roma_dispatches);

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(timeout, absl::Milliseconds(kTestAdRetrievalTimeoutMs));
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  RunReactorWithRequest(
      raw_request, BiddingServiceRuntimeConfig({
                       .enable_buyer_debug_url_generation = false,
                       .ad_retrieval_timeout_ms = kTestAdRetrievalTimeoutMs,
                   }));
}

TEST_F(GenerateBidsReactorTest, PrepareDataForAdRetrievalInputIsCorrect) {
  int num_roma_dispatches = 0;
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillRepeatedly([&num_roma_dispatches](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback batch_callback) {
        ++num_roma_dispatches;

        // First dispatch happens for `prepareDataForAdRetrieval` UDF.
        if (num_roma_dispatches == 1) {
          EXPECT_EQ(batch.size(), 1);
          const auto& inputs = batch[0].input;
          EXPECT_EQ(inputs.size(), kNumPrepareDataForRetrievalUdfArgs);
          EXPECT_EQ(
              *inputs[ArgIndex(
                  PrepareDataForRetrievalUdfArgs::kProtectedAppSignals)],
              absl::StrCat("\"", absl::BytesToHexString(kTestAppInstallSignals),
                           "\""));
          EXPECT_EQ(
              *inputs[ArgIndex(
                  PrepareDataForRetrievalUdfArgs::kProtectedAppSignalsVersion)],
              absl::StrCat(kTestEncodingVersion));
          EXPECT_EQ(*inputs[ArgIndex(
                        PrepareDataForRetrievalUdfArgs::kAuctionSignals)],
                    kTestAuctionSignals);
          EXPECT_EQ(
              *inputs[ArgIndex(PrepareDataForRetrievalUdfArgs::kBuyerSignals)],
              kTestBuyerSignals);

          return MockRomaExecution(batch, std::move(batch_callback),
                                   kPrepareDataForAdRetrievalEntryFunctionName,
                                   kPrepareDataForAdRetrievalBlobVersion,
                                   CreatePrepareDataForAdsRetrievalResponse());
        }
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  RunReactorWithRequest(raw_request);
}

TEST_F(GenerateBidsReactorTest, AdRetrievalClientInputIsCorrect) {
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(dispatcher_, num_roma_dispatches);

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(raw_request->partitions().size(), 1);
        const auto& udf_arguments = raw_request->partitions()[0].arguments();
        EXPECT_EQ(udf_arguments.size(), kNumAdRetrievalUdfArguments);
        auto parsed_retrieval_data = ParseJsonString(
            udf_arguments[kPreparedRetrievalDataIndex].data().string_value());
        auto decoded_signals_itr =
            parsed_retrieval_data->FindMember(kDecodedSignals);
        EXPECT_NE(decoded_signals_itr, parsed_retrieval_data->MemberEnd());
        EXPECT_EQ(std::string(decoded_signals_itr->value.GetString()),
                  kTestDecodedAppInstallSignals);

        auto retrieval_data_itr =
            parsed_retrieval_data->FindMember(kRetrievalData);
        EXPECT_NE(retrieval_data_itr, parsed_retrieval_data->MemberEnd());
        EXPECT_EQ(std::string(retrieval_data_itr->value.GetString()),
                  kTestRetrievalData);

        EXPECT_EQ(
            std::string(
                udf_arguments[kContextualSignalsIndex].data().string_value()),
            kTestBuyerSignals);

        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);

  // Verify the bid returned by the generateBid UDF is the same returned by
  // the reactor.
  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);
}

TEST_F(GenerateBidsReactorTest, GenerateBidsInputIsCorrect) {
  int num_roma_dispatches = 0;
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillRepeatedly([&num_roma_dispatches](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback batch_callback) {
        ++num_roma_dispatches;

        if (num_roma_dispatches == 1) {
          // First dispatch happens for `prepareDataForAdRetrieval` UDF.
          return MockRomaExecution(batch, std::move(batch_callback),
                                   kPrepareDataForAdRetrievalEntryFunctionName,
                                   kPrepareDataForAdRetrievalBlobVersion,
                                   CreatePrepareDataForAdsRetrievalResponse());
        } else {
          // Second dispatch happens for `generateBid` UDF.
          EXPECT_EQ(batch.size(), 1);
          const auto& inputs = batch[0].input;
          EXPECT_EQ(inputs.size(), kNumGenerateBidsUdfArgs);

          // Verify ads match our expectations.
          EXPECT_EQ(*inputs[ArgIndex(GenerateBidsUdfArgs::kAds)],
                    kTestAdsRetrievalAdsResponse);

          EXPECT_EQ(*inputs[ArgIndex(GenerateBidsUdfArgs::kAuctionSignals)],
                    kTestAuctionSignals);
          EXPECT_EQ(*inputs[ArgIndex(GenerateBidsUdfArgs::kBuyerSignals)],
                    kTestBuyerSignals);

          // Verify Contextual Embeddings match our expectations.
          EXPECT_EQ(*inputs[ArgIndex(GenerateBidsUdfArgs::kAds)],
                    kTestAdsRetrievalAdsResponse);
          return MockRomaExecution(batch, std::move(batch_callback),
                                   kGenerateBidEntryFunction,
                                   kProtectedAppSignalsGenerateBidBlobVersion,
                                   CreateGenerateBidsUdfResponse());
        }
      });

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);
}

TEST_F(GenerateBidsReactorTest, EgressPayloadAreNotPopulated) {
  absl::SetFlag(&FLAGS_limited_egress_bits, 0);
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(dispatcher_, num_roma_dispatches);

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);

  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);

  ASSERT_EQ(generated_bid.egress_payload().size(), 0);
}

TEST_F(GenerateBidsReactorTest, ZeroBidsAreFiltered) {
  int num_roma_dispatches = 0;
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillRepeatedly([&num_roma_dispatches](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback batch_callback) {
        ++num_roma_dispatches;

        if (num_roma_dispatches == 1) {
          // First dispatch happens for `prepareDataForAdRetrieval` UDF.
          return MockRomaExecution(batch, std::move(batch_callback),
                                   kPrepareDataForAdRetrievalEntryFunctionName,
                                   kPrepareDataForAdRetrievalBlobVersion,
                                   CreatePrepareDataForAdsRetrievalResponse());
        } else {
          // Second dispatch happens for `generateBid` UDF.
          return MockRomaExecution(
              batch, std::move(batch_callback), kGenerateBidEntryFunction,
              kProtectedAppSignalsGenerateBidBlobVersion,
              CreateGenerateBidsUdfResponse(
                  kTestRenderUrl, /*bid=*/0.0,
                  /*egress_payload_string=*/"",
                  /*debug_reporting_urls=*/"RJSON({})JSON"));
        }
      });

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);

  ASSERT_EQ(raw_response.bids().size(), 0);
}

TEST_F(GenerateBidsReactorTest,
       EmptyAdRetrievalResponseMeansNoGenerateBidAttempt) {
  int num_roma_dispatches = 0;
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillRepeatedly([&num_roma_dispatches](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback batch_callback) {
        ++num_roma_dispatches;

        if (num_roma_dispatches == 1) {
          // First dispatch happens for `prepareDataForAdRetrieval` UDF.
          return MockRomaExecution(batch, std::move(batch_callback),
                                   kPrepareDataForAdRetrievalEntryFunctionName,
                                   kPrepareDataForAdRetrievalBlobVersion,
                                   CreatePrepareDataForAdsRetrievalResponse());
        }
        return absl::OkStatus();
      });

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse("");
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and no dispatch to
  // `generateBids` is expected.
  ASSERT_EQ(num_roma_dispatches, 1);
}

TEST_F(GenerateBidsReactorTest, NoContextualAdsMeansAdRetrievalServiceInvoked) {
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(dispatcher_, num_roma_dispatches);
  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });
  EXPECT_CALL(kv_async_client_, ExecuteInternal).Times(0);
  ContextualProtectedAppSignalsData contextual_pas_data;
  EXPECT_TRUE(contextual_pas_data.ad_render_ids().empty());
  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, std::move(contextual_pas_data));
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);

  // Verify the bid returned by the generateBid UDF is the same returned by
  // the reactor.
  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);
}

TEST_F(GenerateBidsReactorTest, ContextualAdsMeansKVServiceInvoked) {
  int num_roma_dispatches = 0;
  SetupContextualProtectedAppSignalsRomaExpectations(dispatcher_,
                                                     num_roma_dispatches);
  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal).Times(0);
  EXPECT_CALL(kv_async_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });
  ContextualProtectedAppSignalsData contextual_pas_data;
  *contextual_pas_data.mutable_ad_render_ids()->Add() = kTestAdRenderId;
  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, std::move(contextual_pas_data));
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `generateBids` is expected.
  ASSERT_EQ(num_roma_dispatches, 1);

  // Verify the bid returned by the generateBid UDF is the same returned by
  // the reactor.
  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);
}

TEST_F(GenerateBidsReactorTest, KvInputIsCorrect) {
  int num_roma_dispatches = 0;
  SetupContextualProtectedAppSignalsRomaExpectations(dispatcher_,
                                                     num_roma_dispatches);
  EXPECT_CALL(kv_async_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(raw_request->partitions().size(), 1);
        const auto& udf_arguments = raw_request->partitions()[0].arguments();
        EXPECT_EQ(udf_arguments.size(), kNumKVLookupUdfArguments);

        const auto& ad_render_ids =
            udf_arguments[kAdRenderIdsIndex].data().list_value().values();

        EXPECT_EQ(ad_render_ids.size(), 1);
        EXPECT_EQ(ad_render_ids[0].string_value(), kTestAdRenderId);
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  ContextualProtectedAppSignalsData contextual_pas_data;
  *contextual_pas_data.mutable_ad_render_ids()->Add() = kTestAdRenderId;
  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, std::move(contextual_pas_data));
  auto raw_response = RunReactorWithRequest(raw_request);

  // Only a single dispatch to `generateBids` is expected.
  ASSERT_EQ(num_roma_dispatches, 1);

  // Verify the bid returned by the generateBid UDF is the same returned by
  // the reactor.
  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);
}

TEST_F(GenerateBidsReactorTest, TemporaryEgressVectorGetsPopulated) {
  absl::SetFlag(&FLAGS_enable_temporary_unlimited_egress, true);
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(
      dispatcher_, num_roma_dispatches,
      /*prepare_data_for_ad_retrieval_udf_response=*/absl::nullopt,
      absl::Substitute(R"JSON(
        {
        "bid" : $0,
        "render": "$1",
        "temporaryUnlimitedEgressPayload" :
          "{\"features\":
            [
              {\"name\": \"unsigned-integer-feature\", \"value\": 2},
              {\"name\": \"boolean-feature\", \"value\": true},
              {\"name\": \"histogram-feature\",
               \"value\": [
                  {
                    \"name\": \"signed-integer-feature\",
                    \"value\": -1
                  }
                ]
              }
            ]
          }"
        }
      )JSON",
                       kTestWinningBid, kTestRenderUrl));

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, /*contextual_pas_data=*/absl::nullopt,
      /*enable_unlimited_egress=*/true);
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);

  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);

  EXPECT_GT(generated_bid.temporary_unlimited_egress_payload().size(), 0);
}

TEST_F(GenerateBidsReactorTest,
       TemporaryEgressVectorNotPopulatedWhenNotEnabled) {
  absl::SetFlag(&FLAGS_enable_temporary_unlimited_egress, true);
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(dispatcher_, num_roma_dispatches);

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, /*contextual_pas_data=*/absl::nullopt,
      /*enable_unlimited_egress=*/false);
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);

  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);

  EXPECT_EQ(generated_bid.temporary_unlimited_egress_payload().size(), 0);
}

TEST_F(GenerateBidsReactorTest,
       TemporaryEgressVectorNotPopulatedWhenFeatureIsOff) {
  absl::SetFlag(&FLAGS_enable_temporary_unlimited_egress, false);
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(dispatcher_, num_roma_dispatches);

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, /*contextual_pas_data=*/absl::nullopt,
      /*enable_unlimited_egress=*/true);
  auto raw_response = RunReactorWithRequest(raw_request);

  // One dispatch to `preparedDataForAdRetrieval` and another to `generateBids`
  // is expected.
  ASSERT_EQ(num_roma_dispatches, 2);

  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);

  EXPECT_EQ(generated_bid.temporary_unlimited_egress_payload().size(), 0);
}

TEST_F(GenerateBidsReactorTest, SerializesEgressVector) {
  absl::SetFlag(&FLAGS_enable_temporary_unlimited_egress, true);
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(
      dispatcher_, num_roma_dispatches,
      /*prepare_data_for_ad_retrieval_udf_response=*/absl::nullopt,
      absl::Substitute(R"JSON(
        {
        "bid" : $0,
        "render": "$1",
        "temporaryUnlimitedEgressPayload" :
          "{\"features\":
            [
              {\"name\": \"unsigned-integer-feature\", \"value\": 2},
              {\"name\": \"boolean-feature\", \"value\": true},
              {\"name\": \"histogram-feature\",
               \"value\": [
                  {
                    \"name\": \"signed-integer-feature\",
                    \"value\": -1
                  }
                ]
              }
            ]
          }"
        }
      )JSON",
                       kTestWinningBid, kTestRenderUrl));

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, /*contextual_pas_data=*/absl::nullopt,
      /*enable_unlimited_egress=*/true);
  auto raw_response = RunReactorWithRequest(raw_request);

  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);

  // From LSB to MSB: Header is set (including schema version, protocol)
  // bits for each feature are populated (if not explicitly set, then a
  // default of 0 is used).
  //
  // Base64 encoded payload: AYJA
  // Wire representation: 00000001 10000010 01000000
  // In the wire representation, we expect the schema version (2 is used for
  // this test by default) in 3 MSB of header and in the payload, there is a
  // unsigned integer of value 2 with width 7, a single boolean present and a
  // histogram feature with a signed integer of value 1.
  EXPECT_EQ(generated_bid.temporary_unlimited_egress_payload(), "AYJA");
}

TEST_F(GenerateBidsReactorTest, SerializesMultipleFeatures) {
  // Allow a byte of egress.
  absl::SetFlag(&FLAGS_limited_egress_bits, 9);
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(
      dispatcher_, num_roma_dispatches,
      /*prepare_data_for_ad_retrieval_udf_response=*/absl::nullopt,
      absl::Substitute(R"JSON(
        {
        "bid" : $0,
        "render": "$1",
        "egressPayload" :
          "{\"features\": [
              {\"name\": \"boolean-feature\", \"value\": true},
              {\"name\": \"unsigned-integer-feature\", \"value\": 127},
              {\"name\": \"histogram-feature\",
               \"value\": [
                  {
                      \"name\": \"signed-integer-feature\",
                      \"value\": -1
                    }
                  ]
                }
              ]
            }"
        }
      )JSON",
                       kTestWinningBid, kTestRenderUrl));

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, /*contextual_pas_data=*/absl::nullopt,
      /*enable_unlimited_egress=*/true);
  auto raw_response = RunReactorWithRequest(raw_request);

  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);

  // From LSB to MSB: Header is set (including schema version, protocol)
  // bits for each feature are populated (if not explicitly set, then a
  // default of 0 is used).
  //
  // Base64 encoded payload: Af9A
  // Wire representation: 00000001 11111111 01000000
  // In the wire representation, we expect the schema version (2 is used for
  // this test by default) in 3 MSB of header and in the payload, there is a
  // single boolean present, an unsigned integer of value 127 and a histogram
  // feature with a value of 1.
  EXPECT_EQ(generated_bid.egress_payload(), "Af9A");
}

TEST_F(GenerateBidsReactorTest, EgressClearedIfOverLimit) {
  // Allow 7-bits of egress, this will cause the egress vector to be cleared up
  // because actual agress in the test is 8 bits.
  absl::SetFlag(&FLAGS_limited_egress_bits, 7);
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(
      dispatcher_, num_roma_dispatches,
      /*prepare_data_for_ad_retrieval_udf_response=*/absl::nullopt,
      absl::Substitute(R"JSON(
        {
        "bid" : $0,
        "render": "$1",
        "egressPayload" : "{\"features\": [{\"name\": \"boolean-feature\", \"value\": true}, {\"name\": \"unsigned-integer-feature\", \"value\": 127}]}"
        }
      )JSON",
                       kTestWinningBid, kTestRenderUrl));

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, /*contextual_pas_data=*/absl::nullopt,
      /*enable_unlimited_egress=*/true);
  auto raw_response = RunReactorWithRequest(raw_request);

  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);

  EXPECT_EQ(generated_bid.egress_payload(), "");
}

TEST_F(GenerateBidsReactorTest, ReturnsEmptyEgressVectorWhenNonePresent) {
  absl::SetFlag(&FLAGS_enable_temporary_unlimited_egress, true);
  int num_roma_dispatches = 0;
  SetupProtectedAppSignalsRomaExpectations(
      dispatcher_, num_roma_dispatches,
      /*prepare_data_for_ad_retrieval_udf_response=*/absl::nullopt,
      absl::Substitute(R"JSON(
        {
        "bid" : $0,
        "render": "$1",
        "temporaryUnlimitedEgressPayload" : "",
        "egressPayload": ""
        }
      )JSON",
                       kTestWinningBid, kTestRenderUrl));

  EXPECT_CALL(ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto response = CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });

  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName, /*contextual_pas_data=*/absl::nullopt,
      /*enable_unlimited_egress=*/true);
  auto raw_response = RunReactorWithRequest(raw_request);

  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& generated_bid = raw_response.bids()[0];
  EXPECT_EQ(generated_bid.bid(), kTestWinningBid);
  EXPECT_EQ(generated_bid.render(), kTestRenderUrl);

  EXPECT_EQ(generated_bid.temporary_unlimited_egress_payload(), "");
  EXPECT_EQ(generated_bid.egress_payload(), "");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
