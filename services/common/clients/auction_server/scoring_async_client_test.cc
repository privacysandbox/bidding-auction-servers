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

#include "services/common/clients/auction_server/scoring_async_client.h"

#include <memory>
#include <string>
#include <utility>

#include <include/gmock/gmock-actions.h>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::protobuf::util::MessageDifferencer;

namespace {

class ScoringAsyncClientTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(ScoringAsyncClientTest, CallServerWithEncryptionEnabled_Success) {
  using ServiceThread =
      MockServerThread<AuctionServiceMock, ScoreAdsRequest, ScoreAdsResponse>;
  // Hold onto a copy of the request that the server gets; we'll validate the
  // request object was constructed properly down below.
  ScoreAdsRequest received_request;

  ScoreAdsResponse mock_server_response;
  mock_server_response.set_response_ciphertext("response_ciphertext");

  // Start a mocked instance Auction Service.
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&](grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
          ScoreAdsResponse* response) {
        received_request = *request;
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        response->MergeFrom(mock_server_response);
        return reactor;
      });

  AuctionServiceClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
      .compression = false,
      .secure_client = true};

  // Return an empty key.
  server_common::MockKeyFetcherManager key_fetcher_manager;
  google::cmrt::sdk::public_key_service::v1::PublicKey public_key;
  public_key.set_key_id("key_id");
  EXPECT_CALL(key_fetcher_manager, GetPublicKey)
      .WillOnce(testing::Return(public_key));

  MockCryptoClientWrapper crypto_client;
  // Mock the HpkeEncrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptedData data;
  data.set_ciphertext("request_ciphertext");
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse encrypt_response;
  *encrypt_response.mutable_encrypted_data() = std::move(data);
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .WillOnce(testing::Return(encrypt_response));
  // Mock the AeadDecrypt() call on the crypto_client. The payload in the
  // response should be binary proto that can be parsed by the client.
  google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse decrypt_response;
  ScoreAdsResponse_AdScore ad_score;
  ad_score.set_interest_group_name("ig_name");
  ScoreAdsResponse_ScoreAdsRawResponse score_ads_raw_response;
  *score_ads_raw_response.mutable_ad_score() = ad_score;
  decrypt_response.set_payload(score_ads_raw_response.SerializeAsString());
  EXPECT_CALL(crypto_client, AeadDecrypt)
      .WillOnce(testing::Return(decrypt_response));

  ScoringAsyncGrpcClient class_under_test(&key_fetcher_manager, &crypto_client,
                                          client_config);
  ScoreAdsRequest::ScoreAdsRawRequest request;

  grpc::ClientContext context;
  absl::Notification notification;
  absl::Status execute_result = class_under_test.ExecuteInternal(
      std::make_unique<ScoreAdsRequest::ScoreAdsRawRequest>(std::move(request)),
      &context,
      [&](absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
              callback_response,
          ResponseMetadata response_metadata) {
        std::string difference;
        google::protobuf::util::MessageDifferencer differencer;
        differencer.ReportDifferencesToString(&difference);
        // Verify that after decryption, the raw_response field was set
        // correctly before executing the callback was executed. In this case,
        // we're verifying the 'interest_group_name' field is being set.
        EXPECT_TRUE(
            differencer.Compare(**callback_response, score_ads_raw_response))
            << difference;
        notification.Notify();
      });
  EXPECT_TRUE(execute_result.ok());
  notification.WaitForNotification();

  // Verify the mocked Auction Service received the ciphertext.
  ScoreAdsRequest expected;
  expected.set_request_ciphertext("request_ciphertext");
  expected.set_key_id("key_id");
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      expected, received_request));
}

TEST_F(ScoringAsyncClientTest,
       CallServerWithEncryptionEnabled_EncryptionFailure) {
  using ServiceThread =
      MockServerThread<AuctionServiceMock, ScoreAdsRequest, ScoreAdsResponse>;
  ScoreAdsResponse mock_server_response;

  // Start a mocked instance Auction Service.
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&](grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
          ScoreAdsResponse* response) { return context->DefaultReactor(); });

  AuctionServiceClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
      .compression = false,
      .secure_client = true};

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPublicKey)
      .WillOnce(testing::Return(absl::InternalError("failure")));
  MockCryptoClientWrapper crypto_client;
  ScoringAsyncGrpcClient class_under_test(&key_fetcher_manager, &crypto_client,
                                          client_config);

  grpc::ClientContext context;
  absl::Status execute_result = class_under_test.ExecuteInternal(
      std::make_unique<ScoreAdsRequest::ScoreAdsRawRequest>(), &context,
      [](absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
             callback_response,
         ResponseMetadata response_metadata) {  // Never called
      });
  EXPECT_FALSE(execute_result.ok());
}

TEST_F(ScoringAsyncClientTest,
       CallServerWithEncryptionEnabled_DecryptionFailure) {
  using ServiceThread =
      MockServerThread<AuctionServiceMock, ScoreAdsRequest, ScoreAdsResponse>;
  // Hold onto a copy of the request that the server gets; we'll validate the
  // request object was constructed properly down below.
  ScoreAdsRequest received_request;

  ScoreAdsResponse mock_server_response;
  mock_server_response.set_response_ciphertext("response_ciphertext");

  // Start a mocked instance Auction Service.
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&](grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
          ScoreAdsResponse* response) {
        received_request = *request;
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        response->MergeFrom(mock_server_response);
        return reactor;
      });

  AuctionServiceClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
      .compression = false,
      .secure_client = true};

  // Return an empty key.
  server_common::MockKeyFetcherManager key_fetcher_manager;
  google::cmrt::sdk::public_key_service::v1::PublicKey public_key;
  public_key.set_key_id("key_id");
  EXPECT_CALL(key_fetcher_manager, GetPublicKey)
      .WillOnce(testing::Return(public_key));

  MockCryptoClientWrapper crypto_client;
  // Mock the HpkeEncrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptedData data;
  data.set_ciphertext("request_ciphertext");
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse encrypt_response;
  *encrypt_response.mutable_encrypted_data() = std::move(data);
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .WillOnce(testing::Return(encrypt_response));
  // Fail on the AeadDecrypt() call.
  EXPECT_CALL(crypto_client, AeadDecrypt)
      .WillOnce(testing::Return(absl::InternalError("failure")));

  ScoringAsyncGrpcClient class_under_test(&key_fetcher_manager, &crypto_client,
                                          client_config);
  ScoreAdsRequest::ScoreAdsRawRequest request;

  grpc::ClientContext context;
  absl::Notification notification;
  absl::Status execute_result = class_under_test.ExecuteInternal(
      std::make_unique<ScoreAdsRequest::ScoreAdsRawRequest>(std::move(request)),
      &context,
      [&](absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
              callback_response,
          ResponseMetadata response_metadata) {
        EXPECT_FALSE(callback_response.ok());
        notification.Notify();
      });
  EXPECT_TRUE(execute_result.ok());
  notification.WaitForNotification();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
