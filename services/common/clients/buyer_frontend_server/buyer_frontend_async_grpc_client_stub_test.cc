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

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "quiche/common/quiche_data_reader.h"
#include "services/common/chaffing/transcoding_utils.h"
#include "services/common/clients/async_grpc/default_async_grpc_client_stub_test.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/seller_frontend_service/runtime_flags.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ServiceThread =
    MockServerThread<BuyerFrontEndServiceMock, GetBidsRequest, GetBidsResponse>;

using BuyerFrontEndImplementationType =
    ::testing::Types<AsyncGrpcClientTypeDefinitions<
        GetBidsRequest, GetBidsRequest::GetBidsRawRequest, GetBidsResponse,
        GetBidsResponse::GetBidsRawResponse, ServiceThread,
        BuyerFrontEndAsyncGrpcClient, BuyerServiceClientConfig>>;

INSTANTIATE_TYPED_TEST_SUITE_P(BuyerFrontEndAsyncGrpcClientStubTest,
                               AsyncGrpcClientStubTest,
                               BuyerFrontEndImplementationType);

TEST(BuyerFrontEndAsyncGrpcClientStubTest,
     ChaffingEnabled_VerifyChaffRequestFormat) {
  GetBidsResponse mock_bfe_response;
  mock_bfe_response.set_response_ciphertext("sample_ciphertext");

  // Hold onto a copy of the ciphertext that the BFE receives; we validate its
  // encoding below.
  std::string bfe_received_request_ciphertext;

  // Set up fake BFE on local thread.
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&bfe_received_request_ciphertext, &mock_bfe_response](
          grpc::CallbackServerContext* context, const GetBidsRequest* request,
          GetBidsResponse* response) {
        bfe_received_request_ciphertext = request->request_ciphertext();
        response->CopyFrom(mock_bfe_response);
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });
  BuyerServiceClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
      .chaffing_enabled = true};

  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);

  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_is_chaff(true);

  RequestConfig request_config = {.chaff_request_size = 100,
                                  .compression_type = CompressionType::kGzip};

  MockCryptoClientWrapper crypto_client;
  // Mock the HpkeEncrypt() call on the crypto client.
  absl::StatusOr<std::string> encoded_raw_request =
      EncodeAndCompressGetBidsPayload(raw_request, CompressionType::kGzip,
                                      request_config.chaff_request_size);
  ASSERT_TRUE(encoded_raw_request.ok());
  MockHpkeEncryptCall(crypto_client, *encoded_raw_request);
  // Mock the AeadDecrypt() call on the crypto client.
  GetBidsResponse::GetBidsRawResponse mock_bfe_raw_response;

  absl::StatusOr<std::string> encoded_mock_bfe_raw_response =
      EncodeAndCompressGetBidsPayload(mock_bfe_raw_response,
                                      CompressionType::kGzip, 50);
  ASSERT_TRUE(encoded_mock_bfe_raw_response.ok());
  MockAeadDecryptCall(crypto_client, *encoded_mock_bfe_raw_response);

  // Set up the BuyerFrontEndAsyncGrpcClient for the test.
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  BuyerFrontEndAsyncGrpcClient client(key_fetcher_manager.get(), &crypto_client,
                                      client_config);

  // Run the test.
  grpc::ClientContext context;
  absl::Notification notification;
  auto status = client.ExecuteInternal(
      std::make_unique<GetBidsRequest::GetBidsRawRequest>(raw_request),
      &context,
      [&notification, &mock_bfe_response](
          absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
              response,
          ResponseMetadata response_metadata) {
        // Verify that response_size in the ResponseMetadata correctly is
        // populated correctly. It should match the mock_bfe_response we had the
        // dummy thread/reactor return.
        EXPECT_EQ(response_metadata.response_size,
                  mock_bfe_response.ByteSizeLong());
        // We pass an empty GetBidsRawResponse up to the reactor for chaff
        // requests.
        EXPECT_EQ((*response)->ByteSizeLong(), 0);

        notification.Notify();
      },
      kMaxClientTimeout, request_config);
  CHECK_OK(status);
  notification.WaitForNotification();

  // Decode the GetBidsRequest received by our mock BFE and verify it matches
  // the request we sent.
  absl::StatusOr<DecodedGetBidsPayload<GetBidsRequest::GetBidsRawRequest>>
      received_request =
          DecodeGetBidsPayload<GetBidsRequest::GetBidsRawRequest>(
              bfe_received_request_ciphertext);
  ASSERT_TRUE(received_request.ok());
  // Version bits are 0 for now.
  EXPECT_EQ(received_request->version, 0);
  EXPECT_EQ(received_request->compression_type, CompressionType::kGzip);

  std::string get_bids_raw_req_diff;
  google::protobuf::util::MessageDifferencer get_bids_raw_req_differencer;
  get_bids_raw_req_differencer.ReportDifferencesToString(
      &get_bids_raw_req_diff);
  EXPECT_TRUE(get_bids_raw_req_differencer.Compare(
      received_request->get_bids_proto, raw_request))
      << "\nActual:\n"
      << received_request->get_bids_proto.DebugString() << "\n\nExpected:\n"
      << raw_request.DebugString() << "\n\nDifference:\n"
      << get_bids_raw_req_diff;
}

TEST(BuyerFrontEndAsyncGrpcClientStubTest,
     ChaffingDisabled_VerifyNonChaffRequestFormat) {
  // Create a GetBidsResponse for our mock BFE to return.
  AdWithBid ad_with_bid;
  ad_with_bid.set_render("foo");
  GetBidsResponse::GetBidsRawResponse mock_bfe_raw_response;
  mock_bfe_raw_response.mutable_bids()->Add(std::move(ad_with_bid));
  GetBidsResponse mock_bfe_response;
  mock_bfe_response.set_response_ciphertext(*Compress(
      mock_bfe_raw_response.SerializeAsString(), CompressionType::kGzip));

  // Hold onto a copy of the ciphertext that the BFE receives; we validate its
  // encoding below.
  std::string bfe_received_request_ciphertext;

  // Set up fake BFE on local thread.
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&bfe_received_request_ciphertext, &mock_bfe_response](
          grpc::CallbackServerContext* context, const GetBidsRequest* request,
          GetBidsResponse* response) {
        bfe_received_request_ciphertext = request->request_ciphertext();
        response->CopyFrom(mock_bfe_response);
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });
  BuyerServiceClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
      .chaffing_enabled = false};

  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);

  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_is_chaff(false);

  RequestConfig request_config = {.chaff_request_size = 0,
                                  .compression_type = CompressionType::kGzip};

  MockCryptoClientWrapper crypto_client;
  // Mock the HpkeEncrypt() call on the crypto client.
  MockHpkeEncryptCall(crypto_client, *Compress(raw_request.SerializeAsString(),
                                               CompressionType::kGzip));
  // Mock the AeadDecrypt() call on the crypto client.
  // Call Compress() since the client will try to decompress the result.
  MockAeadDecryptCall(crypto_client,
                      *Compress(mock_bfe_raw_response.SerializeAsString(),
                                CompressionType::kGzip));

  // Set up the BuyerFrontEndAsyncGrpcClient for the test.
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  BuyerFrontEndAsyncGrpcClient client(key_fetcher_manager.get(), &crypto_client,
                                      client_config);

  // Run the test.
  grpc::ClientContext context;
  absl::Notification notification;
  auto status = client.ExecuteInternal(
      std::make_unique<GetBidsRequest::GetBidsRawRequest>(raw_request),
      &context,
      [&notification, &mock_bfe_response, &mock_bfe_raw_response](
          absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
              response,
          ResponseMetadata response_metadata) {
        // Verify that response_size in the ResponseMetadata correctly is
        // populated correctly. It should match the mock_bfe_response we had the
        // dummy thread/reactor return.
        EXPECT_EQ(response_metadata.response_size,
                  mock_bfe_response.ByteSizeLong());
        // We pass an empty proto up to the reactor for chaff requests.
        EXPECT_EQ((*response)->ByteSizeLong(),
                  mock_bfe_raw_response.ByteSizeLong());

        notification.Notify();
      },
      kMaxClientTimeout, request_config);
  CHECK_OK(status);
  notification.WaitForNotification();

  // Decode the GetBidsRequest received by our mock BFE and verify it matches
  // the request we sent. For this test, the request should actually just be an
  // empty string because the only field on the request was is_chaff to be
  // false, which it is by default.
  absl::StatusOr<std::string> received_request = Decompress(
      std::move(bfe_received_request_ciphertext), CompressionType::kGzip);
  ASSERT_TRUE(received_request.ok());

  GetBidsResponse::GetBidsRawResponse actual;
  ASSERT_TRUE(actual.ParseFromString(*received_request));
  EXPECT_EQ(actual.ByteSizeLong(), 0);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
