// Copyright 2024 Google LLC
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

#include "services/seller_frontend_service/select_auction_result_reactor.h"

#include <gmock/gmock.h>

#include "absl/synchronization/notification.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gtest/gtest.h"
#include "services/common/test/random.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using google::scp::core::test::EqualsProto;

SelectAdResponse RunRequest(const TrustedServersConfigClient& config_client,
                            const ClientRegistry& clients,
                            const SelectAdRequest& request) {
  grpc::CallbackServerContext context;
  SelectAdResponse response;
  SelectAuctionResultReactor reactor(&context, &request, &response, clients,
                                     config_client);
  reactor.Execute();
  return response;
}

template <typename T>
class SelectAuctionResultReactorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetupRequest();
    config_.SetOverride("", CONSENTED_DEBUG_TOKEN);
    config_.SetOverride(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_AUDIENCE);

    // Return hard coded key for decryption.
    EXPECT_CALL(key_fetcher_manager_, GetPrivateKey)
        .Times(testing::AnyNumber())
        .WillRepeatedly(
            [](const google::scp::cpio::PublicPrivateKeyPairId& key_id) {
              EXPECT_EQ(key_id, std::to_string(HpkeKeyset{}.key_id));
              return GetPrivateKey();
            });

    // Initialization for telemetry.
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<SelectAdRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request_);
  }

  void SetupRequest() {
    auto [protected_auction_input, request, context] =
        GetSampleSelectAdRequest<T>(CLIENT_TYPE_ANDROID, kSellerOriginDomain);
    protected_auction_input_ = std::move(protected_auction_input);
    request_ = std::move(request);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(context));
  }

  void SetupComponentAuctionResults(int num = 10) {
    // The key that will be returned by mock key fetcher.
    auto key_id = std::to_string(HpkeKeyset{}.key_id);
    for (int i = 0; i < num; ++i) {
      AuctionResult ar = MakeARandomComponentAuctionResult(
          protected_auction_input_.generation_id(), kSellerOriginDomain);
      auto* car = this->request_.mutable_component_auction_results()->Add();
      car->set_key_id(key_id);
      car->set_auction_result_ciphertext(
          FrameAndCompressProto(ar.SerializeAsString()));
      component_auction_results_.push_back(std::move(ar));
    }
    // Return plaintext as is.
    EXPECT_CALL(crypto_client_, HpkeDecrypt)
        .WillRepeatedly([](const server_common::PrivateKey& private_key,
                           const std::string& ciphertext) {
          // Mock the HpkeDecrypt() call on the crypto client.
          google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
              hpke_decrypt_response;
          hpke_decrypt_response.set_payload(ciphertext);
          return hpke_decrypt_response;
        });
  }

  T protected_auction_input_;
  std::vector<AuctionResult> component_auction_results_;
  SelectAdRequest request_;
  std::unique_ptr<quiche::ObliviousHttpRequest::Context> context_;
  TrustedServersConfigClient config_ = CreateConfig();
  server_common::MockKeyFetcherManager key_fetcher_manager_;
  MockCryptoClientWrapper crypto_client_;
  // Scoring Client
  ScoringAsyncClientMock scoring_client_;
};

using ProtectedAuctionInputTypes =
    ::testing::Types<ProtectedAudienceInput, ProtectedAuctionInput>;
TYPED_TEST_SUITE(SelectAuctionResultReactorTest, ProtectedAuctionInputTypes);

TYPED_TEST(SelectAuctionResultReactorTest, CallsScoringWithComponentAuctions) {
  absl::Notification scoring_done;
  this->SetupComponentAuctionResults(2);
  EXPECT_CALL(this->scoring_client_, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [this, &scoring_done](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                  score_ads_request,
              grpc::ClientContext* context,
              absl::AnyInvocable<void(
                  absl::StatusOr<
                      std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>,
                  ResponseMetadata)&&>
                  on_done,
              absl::Duration timeout, RequestConfig request_config) {
            EXPECT_EQ(score_ads_request->auction_signals(),
                      this->request_.auction_config().auction_signals());
            EXPECT_EQ(score_ads_request->seller_signals(),
                      this->request_.auction_config().seller_signals());
            EXPECT_EQ(score_ads_request->seller(),
                      this->request_.auction_config().seller());
            EXPECT_EQ(score_ads_request->seller_currency(),
                      this->request_.auction_config().seller_currency());
            EXPECT_EQ(score_ads_request->component_auction_results_size(),
                      this->request_.component_auction_results_size());
            for (int i = 0;
                 i < score_ads_request->component_auction_results_size(); i++) {
              // bidding groups are not sent to Auction server.
              this->component_auction_results_[i].clear_bidding_groups();
              this->component_auction_results_[i].clear_update_groups();
              EXPECT_THAT(score_ads_request->component_auction_results(i),
                          EqualsProto(this->component_auction_results_[i]));
            }
            std::move(on_done)(
                std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(),
                /* response_metadata= */ {});
            scoring_done.Notify();
            return absl::OkStatus();
          });
  ClientRegistry clients = {
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>(),
      this->scoring_client_,
      BuyerFrontEndAsyncClientFactoryMock(),
      this->key_fetcher_manager_,
      &this->crypto_client_,
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>())};
  auto response = RunRequest(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
}

TYPED_TEST(SelectAuctionResultReactorTest, AbortsAuctionWithDuplicateResults) {
  absl::Notification scoring_done;
  this->SetupComponentAuctionResults(1);
  // Add duplicate
  const auto& original_car = this->request_.component_auction_results(0);
  auto* duplicate_car =
      this->request_.mutable_component_auction_results()->Add();
  duplicate_car->set_key_id(original_car.key_id());
  duplicate_car->set_auction_result_ciphertext(
      original_car.auction_result_ciphertext());

  EXPECT_CALL(this->scoring_client_, ExecuteInternal).Times(0);
  ClientRegistry clients = {
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>(),
      this->scoring_client_,
      BuyerFrontEndAsyncClientFactoryMock(),
      this->key_fetcher_manager_,
      &this->crypto_client_,
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>())};
  auto response = RunRequest(this->config_, clients, this->request_);
  EXPECT_EQ(response.auction_result_ciphertext().size(), 0);
}

TYPED_TEST(SelectAuctionResultReactorTest,
           ReturnsErrorForNoValidComponentAuctions) {
  this->SetupComponentAuctionResults(0);
  EXPECT_CALL(this->scoring_client_, ExecuteInternal).Times(0);
  ClientRegistry clients = {
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>(),
      this->scoring_client_,
      BuyerFrontEndAsyncClientFactoryMock(),
      this->key_fetcher_manager_,
      &this->crypto_client_,
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>())};
  auto response = RunRequest(this->config_, clients, this->request_);
  EXPECT_EQ(response.auction_result_ciphertext().size(), 0);
}

TYPED_TEST(SelectAuctionResultReactorTest,
           ReturnsResponseOnWinnerFromScoringClient) {
  absl::Notification scoring_done;
  this->SetupComponentAuctionResults(2);
  ScoreAdsResponse::AdScore winner = MakeARandomAdScore(
      /*hob_buyer_entries = */ 2,
      /*rejection_reason_ig_owners = */ 2,
      /*rejection_reason_ig_per_owner = */ 2);
  EXPECT_CALL(this->scoring_client_, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                  score_ads_request,
              grpc::ClientContext* context,
              absl::AnyInvocable<void(
                  absl::StatusOr<
                      std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>,
                  ResponseMetadata)&&>
                  on_done,
              absl::Duration timeout, RequestConfig request_config) {
            auto response =
                std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
            response->mutable_ad_score()->MergeFrom(winner);
            std::move(on_done)(std::move(response),
                               /* response_metadata= */ {});
            scoring_done.Notify();
            return absl::OkStatus();
          });
  ClientRegistry clients = {
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>(),
      this->scoring_client_,
      BuyerFrontEndAsyncClientFactoryMock(),
      this->key_fetcher_manager_,
      &this->crypto_client_,
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>())};
  auto response = RunRequest(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
  // Conversion from ad score to auction result ciphertext will be
  // unit tested in util function unit tests.
  EXPECT_GT(response.auction_result_ciphertext().size(), 0);
}

TYPED_TEST(SelectAuctionResultReactorTest,
           DoesNotReturnChaffForErrorFromScoringClient) {
  absl::Notification scoring_done;
  this->SetupComponentAuctionResults(2);
  EXPECT_CALL(this->scoring_client_, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                  score_ads_request,
              grpc::ClientContext* context,
              absl::AnyInvocable<void(
                  absl::StatusOr<
                      std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>,
                  ResponseMetadata)&&>
                  on_done,
              absl::Duration timeout, RequestConfig request_config) {
            scoring_done.Notify();
            return absl::Status(absl::StatusCode::kInternal, "test msg");
          });
  ClientRegistry clients = {
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>(),
      this->scoring_client_,
      BuyerFrontEndAsyncClientFactoryMock(),
      this->key_fetcher_manager_,
      &this->crypto_client_,
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>())};
  auto response = RunRequest(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
  // Conversion from ad score to auction result ciphertext will be
  // unit tested in util function unit tests.
  EXPECT_EQ(response.auction_result_ciphertext().size(), 0);
}

TYPED_TEST(SelectAuctionResultReactorTest,
           DoesNotReturnChaffIfSellerNotConfiguredInConfig) {
  this->SetupComponentAuctionResults(2);
  EXPECT_CALL(this->scoring_client_, ExecuteInternal).Times(0);
  ClientRegistry clients = {
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>(),
      this->scoring_client_,
      BuyerFrontEndAsyncClientFactoryMock(),
      this->key_fetcher_manager_,
      &this->crypto_client_,
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>())};
  auto config = CreateConfig();
  config.SetOverride("", SELLER_ORIGIN_DOMAIN);
  auto response = RunRequest(config, clients, this->request_);
  // Conversion from ad score to auction result ciphertext will be
  // unit tested in util function unit tests.
  EXPECT_EQ(response.auction_result_ciphertext().size(), 0);
}

TYPED_TEST(SelectAuctionResultReactorTest,
           DoesNotReturnChaffForNonInternalErrorFromAuctionServer) {
  absl::Notification scoring_done;
  this->SetupComponentAuctionResults(2);
  EXPECT_CALL(this->scoring_client_, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                  score_ads_request,
              grpc::ClientContext* context,
              absl::AnyInvocable<void(
                  absl::StatusOr<
                      std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>,
                  ResponseMetadata)&&>
                  on_done,
              absl::Duration timeout, RequestConfig request_config) {
            std::move(on_done)(
                absl::Status(absl::StatusCode::kInvalidArgument, "test msg"),
                {});
            scoring_done.Notify();
            return absl::OkStatus();
          });
  ClientRegistry clients = {
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>(),
      this->scoring_client_,
      BuyerFrontEndAsyncClientFactoryMock(),
      this->key_fetcher_manager_,
      &this->crypto_client_,
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>())};
  auto response = RunRequest(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
  EXPECT_EQ(response.auction_result_ciphertext().size(), 0);
}

TYPED_TEST(SelectAuctionResultReactorTest,
           ReturnsResponseOnInternalErrorFromAuctionServer) {
  absl::Notification scoring_done;
  this->SetupComponentAuctionResults(2);
  EXPECT_CALL(this->scoring_client_, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                  score_ads_request,
              grpc::ClientContext* context,
              absl::AnyInvocable<void(
                  absl::StatusOr<
                      std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>,
                  ResponseMetadata)&&>
                  on_done,
              absl::Duration timeout, RequestConfig request_config) {
            std::move(on_done)(
                absl::Status(absl::StatusCode::kInternal, "test msg"),
                /* response_metadata= */ {});
            scoring_done.Notify();
            return absl::OkStatus();
          });
  ClientRegistry clients = {
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>(),
      this->scoring_client_,
      BuyerFrontEndAsyncClientFactoryMock(),
      this->key_fetcher_manager_,
      &this->crypto_client_,
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>())};
  auto response = RunRequest(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
  // Conversion from ad score to auction result ciphertext will be
  // unit tested in util function unit tests.
  EXPECT_GT(response.auction_result_ciphertext().size(), 0);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
