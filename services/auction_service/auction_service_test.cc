//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/auction_service/auction_service.h"

#include <gmock/gmock-matchers.h>

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/server.h>

#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service_integration_test_util.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/score_ads_reactor.h"
#include "services/auction_service/score_ads_reactor_test_util.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ::testing::AnyNumber;
using ::testing::HasSubstr;
using ::testing::InSequence;

constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
constexpr char kTestUrl[] = "https://test-ads-url.com";
constexpr char kInterestGroupOwner[] = "test_ig_owner";
constexpr char kTestBuyerSignals[] = "{\"test_key\":\"test_value\"}";
constexpr char kTestRenderUrl[] = "http://test-render-url";
constexpr char kTestSellerSignals[] =
    R"json({"seller_signal": "test_seller_signals"})json";
constexpr char kTestAuctionSignals[] =
    R"json({"auction_signal": "test_auction_signals"})json";
constexpr char kTestPublisherHostname[] = "test_publisher_hostname";
constexpr char kRomaTestError[] = "roma_test_error";
constexpr char kReportingDispatchHandlerFunctionName[] =
    "reportingEntryFunction";

class AuctionServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<ScoreAdsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
    SetupMockCryptoClientWrapper();
    server_common::log::SetGlobalPSVLogLevel(10);

    request_.set_key_id(kKeyId);
  }

  void SetupMockCryptoClientWrapper() {
    // Mock the HpkeDecrypt() call on the crypto_client_. This is used by the
    // service to decrypt the incoming request_.
    EXPECT_CALL(*crypto_client_, HpkeDecrypt)
        .Times(AnyNumber())
        .WillRepeatedly([](const server_common::PrivateKey& private_key,
                           const std::string& ciphertext) {
          google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
              hpke_decrypt_response;
          hpke_decrypt_response.set_payload(ciphertext);
          hpke_decrypt_response.set_secret(kSecret);
          return hpke_decrypt_response;
        });

    // Mock the AeadEncrypt() call on the crypto_client_. This is used to
    // encrypt the response_ coming back from the service.
    EXPECT_CALL(*crypto_client_, AeadEncrypt)
        .Times(AnyNumber())
        .WillOnce([](const std::string& plaintext_payload,
                     const std::string& secret) {
          google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
          data.set_ciphertext(plaintext_payload);
          google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
              aead_encrypt_response;
          *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
          return aead_encrypt_response;
        });
  }

  MockV8DispatchClient dispatcher_;
  ScoreAdsRequest request_;
  ScoreAdsResponse response_;
  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  TrustedServersConfigClient config_client_{{}};
  std::unique_ptr<MockAsyncReporter> async_reporter_ =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
};

TEST_F(AuctionServiceTest, InstantiatesScoreAdsReactor) {
  absl::BlockingCounter init_pending(1);
  auto score_ads_reactor_factory =
      [this, &init_pending](
          grpc::CallbackServerContext* context_,
          const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client_,
          const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger =
            std::make_unique<ScoreAdsNoOpLogger>();
        auto mock = std::make_unique<MockScoreAdsReactor>(
            context_, dispatcher_, request_, response_, key_fetcher_manager,
            crypto_client_, runtime_config, std::move(benchmarkingLogger),
            async_reporter_.get(), "");
        EXPECT_CALL(*mock, Execute).Times(1);
        init_pending.DecrementCount();
        return mock;
      };

  config_client_.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      config_client_, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);
  grpc::CallbackServerContext context;
  std::unique_ptr<grpc::ServerUnaryReactor> reactor(
      service.ScoreAds(&context, &request_, &response_));
  init_pending.Wait();
}

ScoreAdsRequest::ScoreAdsRawRequest BuildScoreAdsRawRequest() {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  raw_request.mutable_per_buyer_signals()->try_emplace(kInterestGroupOwner,
                                                       kTestBuyerSignals);

  AdWithBidMetadata ad_with_bid_metadata;
  ad_with_bid_metadata.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(kTestRenderUrl, "test_metadata", 1));
  ad_with_bid_metadata.set_render(kTestRenderUrl);
  ad_with_bid_metadata.set_bid(10.10);
  ad_with_bid_metadata.set_interest_group_name("test_interest_group");
  ad_with_bid_metadata.set_interest_group_owner(kInterestGroupOwner);
  std::string scoring_signals = absl::StrFormat(R"JSON(
  {
    "renderUrls": {
      "%s": [
          123
        ]
    }
  })JSON",
                                                kTestRenderUrl);

  *raw_request.add_ad_bids() = std::move(ad_with_bid_metadata);
  raw_request.set_seller_signals(kTestSellerSignals);
  raw_request.set_auction_signals(kTestAuctionSignals);
  raw_request.set_scoring_signals(std::move(scoring_signals));
  raw_request.set_publisher_hostname(kTestPublisherHostname);
  return raw_request;
}

TEST_F(AuctionServiceTest, SendsEmptyResponseIfNoAdIsDesirable) {
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        std::vector<absl::StatusOr<DispatchResponse>> responses;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) != 0) {
            responses.emplace_back(DispatchResponse{
                .id = request.id,
                .resp = R"({
                  "response" : {
                    "desirability" : 0,
                    "allowComponentAuction" : false
                  },
                  "logs":[]
                })",
            });
          } else {
            responses.emplace_back(DispatchResponse{
                .id = request.id,
                .resp = "{}",
            });
          }
        }
        done_callback(responses);
        return absl::OkStatus();
      });
  auto score_ads_reactor_factory =
      [this](grpc::CallbackServerContext* context_,
             const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client_,
             const AuctionServiceRuntimeConfig& runtime_config) {
        return std::make_unique<ScoreAdsReactor>(
            context_, dispatcher_, request_, response_,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client_, async_reporter_.get(), runtime_config);
      };

  config_client_.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client_, /*public_key_fetcher=*/nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  request_.set_key_id(kKeyId);
  ScoreAdsRequest::ScoreAdsRawRequest raw_request = BuildScoreAdsRawRequest();
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  grpc::Status status = stub->ScoreAds(&context, request_, &response_);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::OK)
      << status.error_message();
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response_.response_ciphertext()));
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(AuctionServiceTest, RomaExecutionErrorIsPropagated) {
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        return absl::InternalError(kRomaTestError);
      });
  auto score_ads_reactor_factory =
      [this](grpc::CallbackServerContext* context_,
             const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client_,
             const AuctionServiceRuntimeConfig& runtime_config) {
        return std::make_unique<ScoreAdsReactor>(
            context_, dispatcher_, request_, response_,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client_, async_reporter_.get(), runtime_config);
      };

  config_client_.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client_, /*public_key_fetcher=*/nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  request_.set_key_id(kKeyId);
  ScoreAdsRequest::ScoreAdsRawRequest raw_request = BuildScoreAdsRawRequest();
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  grpc::Status status = stub->ScoreAds(&context, request_, &response_);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::UNKNOWN)
      << status.error_message();
  EXPECT_THAT(status.error_message(), HasSubstr(kRomaTestError));
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response_.response_ciphertext()));
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(AuctionServiceTest, AbortsIfMissingScoringSignals) {
  auto score_ads_reactor_factory =
      [this](grpc::CallbackServerContext* context_,
             const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client_,
             const AuctionServiceRuntimeConfig& runtime_config) {
        return std::make_unique<ScoreAdsReactor>(
            context_, dispatcher_, request_, response_,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client_, async_reporter_.get(), runtime_config);
      };

  config_client_.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      config_client_, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  request_.set_key_id(kKeyId);
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  *raw_request.mutable_ad_bids()->Add() = MakeARandomAdWithBidMetadata(1, 10);
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  grpc::Status status = stub->ScoreAds(&context, request_, &response_);

  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_EQ(status.error_message(), kNoTrustedScoringSignals);
}

TEST_F(AuctionServiceTest, AbortsIfMissingDispatchRequests) {
  auto score_ads_reactor_factory =
      [this](grpc::CallbackServerContext* context_,
             const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client_,
             const AuctionServiceRuntimeConfig& runtime_config) {
        return std::make_unique<ScoreAdsReactor>(
            context_, dispatcher_, request_, response_,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client_, async_reporter_.get(), runtime_config);
      };

  config_client_.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      config_client_, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), auction_service_runtime_config);

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  request_.set_key_id(kKeyId);
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  *raw_request.mutable_ad_bids()->Add() = MakeARandomAdWithBidMetadata(1, 10);
  raw_request.set_scoring_signals(
      R"json({"renderUrls":{"placeholder_url":[123]}})json");
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  grpc::Status status = stub->ScoreAds(&context, request_, &response_);

  EXPECT_EQ(status.error_message(), kNoAdsWithValidScoringSignals);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
}

TEST_F(AuctionServiceTest, ScoresProtectedAppSignals) {
  {
    InSequence s;
    EXPECT_CALL(dispatcher_, BatchExecute)
        .WillOnce([](std::vector<DispatchRequest>& batch,
                     BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
          DispatchResponse response;
          auto incoming_bid =
              request.input[static_cast<int>(ScoreAdArgs::kBid)];
          float bid;
          EXPECT_TRUE(absl::SimpleAtof(*incoming_bid, &bid))
              << "Failed to convert bid to float: " << *incoming_bid;
          response.resp = absl::StrFormat(
              R"JSON(
          {
          "response": {
            "desirability":10,
            "bid":%f
          },
          "logs":["test log"]
          }
        )JSON",
              bid);
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher_, BatchExecute)
        .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                           BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name,
                    kReportingProtectedAppSignalsFunctionName);
          DispatchResponse response;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
  }

  auto score_ads_reactor_factory =
      [this](grpc::CallbackServerContext* context_,
             const ScoreAdsRequest* request_, ScoreAdsResponse* response_,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client_,
             const AuctionServiceRuntimeConfig& runtime_config) {
        return std::make_unique<ScoreAdsReactor>(
            context_, dispatcher_, request_, response_,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client_, async_reporter_.get(), runtime_config);
      };

  config_client_.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client_, /* public_key_fetcher=*/nullptr);
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client_), {.enable_protected_app_signals = true});

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  *raw_request.mutable_protected_app_signals_ad_bids()->Add() =
      GetProtectedAppSignalsAdWithBidMetadata(kTestUrl);
  raw_request.set_scoring_signals(
      R"json({"renderUrls":{"https://test-ads-url.com":[123]}})json");
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  grpc::Status status = stub->ScoreAds(&context, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  ABSL_LOG(INFO) << "Scored ad is:\n" << raw_response.DebugString();
  const auto& ad_score = raw_response.ad_score();
  EXPECT_EQ(ad_score.ad_type(), AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD);
  EXPECT_EQ(ad_score.interest_group_owner(), kTestProtectedAppSignalsAdOwner);
  EXPECT_EQ(ad_score.interest_group_name(), "");
  EXPECT_EQ(ad_score.desirability(), 10);
  EXPECT_EQ(ad_score.render(), kTestUrl);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
