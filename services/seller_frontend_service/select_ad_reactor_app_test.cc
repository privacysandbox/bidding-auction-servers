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
#include "services/seller_frontend_service/select_ad_reactor_app.h"

#include <math.h>

#include <memory>
#include <set>
#include <utility>

#include <gmock/gmock-matchers.h>
#include <include/gmock/gmock-actions.h>
#include <include/gmock/gmock-nice-strict.h>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/compression/gzip.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/status_macros.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/cpp/communication/encoding_utils.h"
#include "src/cpp/communication/ohttp_utils.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::Return;
using EncodedBueryInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBueryInputs = ::google::protobuf::Map<std::string, BuyerInput>;

template <typename T>
class SelectAdReactorForAppTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::TelemetryConfig::PROD);
    metric::SfeContextMap(server_common::BuildDependentConfig(config_proto));
  }
};

using ProtectedAuctionInputTypes =
    ::testing::Types<ProtectedAudienceInput, ProtectedAuctionInput>;
TYPED_TEST_SUITE(SelectAdReactorForAppTest, ProtectedAuctionInputTypes);

TYPED_TEST(SelectAdReactorForAppTest, VerifyEncoding) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          SelectAdRequest::ANDROID, kNonZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain);

  auto config = CreateConfig();
  config.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config.SetFlagForTest(kFalse, ENABLE_OTEL_BASED_LOGGING);
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = DecryptEncapsulatedResponse(
      encrypted_response.auction_result_ciphertext(),
      request_with_context.context);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status().message();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->GetPlaintextData().size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
  ASSERT_TRUE(unframed_response.ok()) << unframed_response.status().message();

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      GzipDecompress(unframed_response->compressed_data);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));
  EXPECT_FALSE(deserialized_auction_result.is_chaff());
  EXPECT_EQ(deserialized_auction_result.ad_type(),
            AdType::PROTECTED_AUDIENCE_AD);

  // Validate that the bidding groups data is present.
  EXPECT_EQ(deserialized_auction_result.bidding_groups().size(), 1);
  const auto& [observed_buyer, interest_groups] =
      *deserialized_auction_result.bidding_groups().begin();
  EXPECT_EQ(observed_buyer, kSampleBuyer);
  std::set<int> observed_interest_group_indices(interest_groups.index().begin(),
                                                interest_groups.index().end());
  std::set<int> expected_interest_group_indices = {0};
  std::set<int> unexpected_interest_group_indices;
  absl::c_set_difference(
      observed_interest_group_indices, expected_interest_group_indices,
      std::inserter(unexpected_interest_group_indices,
                    unexpected_interest_group_indices.begin()));
  EXPECT_TRUE(unexpected_interest_group_indices.empty());
}

TYPED_TEST(SelectAdReactorForAppTest, VerifyChaffedResponse) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          SelectAdRequest::ANDROID, kZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain);

  auto config = CreateConfig();
  config.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config.SetFlagForTest(kFalse, ENABLE_OTEL_BASED_LOGGING);
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = DecryptEncapsulatedResponse(
      encrypted_response.auction_result_ciphertext(),
      request_with_context.context);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status().message();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->GetPlaintextData().size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
  ASSERT_TRUE(unframed_response.ok()) << unframed_response.status().message();

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      GzipDecompress(unframed_response->compressed_data);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));

  // Validate chaff bit is set in response.
  EXPECT_TRUE(deserialized_auction_result.is_chaff());
}

TYPED_TEST(SelectAdReactorForAppTest, VerifyErrorForProtoDecodingFailure) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          SelectAdRequest::ANDROID, kZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain);
  auto& protected_auction_input = request_with_context.protected_auction_input;
  auto& request = request_with_context.select_ad_request;
  // Set up the encoded cipher text in the request.
  std::string encoded_request = protected_auction_input.SerializeAsString();
  // Corrupt the binary proto so that we can verify a proper error is set in
  // the response.
  encoded_request.data()[0] = 'a';
  absl::StatusOr<std::string> framed_request =
      server_common::EncodeResponsePayload(
          server_common::CompressionType::kGzip, encoded_request,
          GetEncodedDataSize(encoded_request.size()));
  EXPECT_TRUE(framed_request.ok()) << framed_request.status().message();
  auto ohttp_request = CreateValidEncryptedRequest(std::move(*framed_request));
  EXPECT_TRUE(ohttp_request.ok()) << ohttp_request.status().message();
  std::string encrypted_request = ohttp_request->EncapsulateAndSerialize();
  auto context = std::move(*ohttp_request).ReleaseContext();
  *request.mutable_protected_audience_ciphertext() =
      std::move(encrypted_request);

  auto config = CreateConfig();
  config.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config.SetFlagForTest(kFalse, ENABLE_OTEL_BASED_LOGGING);
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(config, clients, request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = DecryptEncapsulatedResponse(
      encrypted_response.auction_result_ciphertext(), context);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status().message();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->GetPlaintextData().size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
  ASSERT_TRUE(unframed_response.ok()) << unframed_response.status().message();

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      GzipDecompress(unframed_response->compressed_data);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();

  // Validate the error message returned in the response.
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));
  EXPECT_EQ(deserialized_auction_result.error().message(),
            kBadProtectedAudienceBinaryProto);
  EXPECT_EQ(deserialized_auction_result.error().code(), 400);

  // Validate chaff bit is not set if there was an input validation error.
  EXPECT_FALSE(deserialized_auction_result.is_chaff());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
