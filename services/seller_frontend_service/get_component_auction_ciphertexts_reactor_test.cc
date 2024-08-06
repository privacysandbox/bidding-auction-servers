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

#include "services/seller_frontend_service/get_component_auction_ciphertexts_reactor.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/test/mocks.h"
#include "services/seller_frontend_service/util/encryption_util.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/core/test/utils/proto_test_utils.h"
#include "src/encryption/key_fetcher/fake_key_fetcher_manager.h"

using google::scp::core::test::EqualsProto;

namespace privacy_sandbox::bidding_auction_servers {

static const char kSampleEncodedTest[] = "sample_protected_auction_encoding";
static const char kSeller1[] = "seller1.example.com";
static const char kSeller2[] = "seller2.example.com";
static const char kUnknownSeller[] = "unknown-seller";

// Helper function to create a sample GetComponentAuctionCiphertextsRequest.
GetComponentAuctionCiphertextsRequest CreateSampleRequest() {
  GetComponentAuctionCiphertextsRequest request;
  auto [ciphertext, context] =
      GetFramedInputAndOhttpContext(kSampleEncodedTest);
  request.set_protected_auction_ciphertext(ciphertext);
  request.add_component_sellers(kSeller1);
  request.add_component_sellers(kSeller2);
  return request;
}

class GetComponentAuctionCiphertextsReactorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    key_fetcher_manager_ =
        std::make_unique<server_common::FakeKeyFetcherManager>();
    seller_cloud_platform_map_ = {
        {kSeller1, server_common::CloudPlatform::kGcp},
        {kSeller2, server_common::CloudPlatform::kAws}};
    valid_request_ = CreateSampleRequest();
  }

  GetComponentAuctionCiphertextsResponse CreateReactorAndRun(
      const GetComponentAuctionCiphertextsRequest& request) {
    GetComponentAuctionCiphertextsResponse response;
    auto class_under_test = GetComponentAuctionCiphertextsReactor(
        &request, &response, *key_fetcher_manager_, seller_cloud_platform_map_);
    class_under_test.Execute();
    return response;
  }

  // Helper function to decrypt and compare a ciphertext in
  // seller_component_ciphertexts map with the original encoded string.
  void DecryptAndTestEncodedText(
      GetComponentAuctionCiphertextsResponse& actual_response,
      absl::string_view seller_name) {
    ASSERT_TRUE(
        actual_response.seller_component_ciphertexts().contains(seller_name))
        << "Ciphertext not found for seller " << seller_name;
    auto decryption_status = DecryptOHTTPEncapsulatedHpkeCiphertext(
        actual_response.seller_component_ciphertexts().at(seller_name),
        *key_fetcher_manager_);
    ASSERT_TRUE(decryption_status.ok()) << decryption_status.status();

    // Unframe the framed response.
    absl::StatusOr<server_common::DecodedRequest> unframed_response =
        server_common::DecodeRequestPayload((*decryption_status)->plaintext);
    ASSERT_TRUE(unframed_response.ok()) << unframed_response.status().message();

    EXPECT_EQ(unframed_response->compressed_data, kSampleEncodedTest);
  }

  GetComponentAuctionCiphertextsRequest valid_request_;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  absl::flat_hash_map<std::string, server_common::CloudPlatform>
      seller_cloud_platform_map_;
};

TEST_F(GetComponentAuctionCiphertextsReactorTest,
       EncryptsCiphertextsForAllSellers) {
  auto actual_response = CreateReactorAndRun(valid_request_);
  ASSERT_EQ(actual_response.seller_component_ciphertexts().size(), 2);
  DecryptAndTestEncodedText(actual_response, kSeller1);
  DecryptAndTestEncodedText(actual_response, kSeller2);
}

TEST_F(GetComponentAuctionCiphertextsReactorTest,
       ReturnsDifferentCiphertextForEachSeller) {
  ASSERT_GT(valid_request_.protected_auction_ciphertext().size(), 0);
  auto actual_response = CreateReactorAndRun(valid_request_);

  ASSERT_EQ(actual_response.seller_component_ciphertexts().size(), 2);
  ASSERT_TRUE(
      actual_response.seller_component_ciphertexts().contains(kSeller1));
  EXPECT_GT(actual_response.seller_component_ciphertexts().at(kSeller1).size(),
            0);
  EXPECT_NE(actual_response.seller_component_ciphertexts().at(kSeller1),
            valid_request_.protected_auction_ciphertext());

  ASSERT_TRUE(
      actual_response.seller_component_ciphertexts().contains(kSeller2));
  EXPECT_GT(actual_response.seller_component_ciphertexts().at(kSeller2).size(),
            0);
  EXPECT_NE(actual_response.seller_component_ciphertexts().at(kSeller2),
            valid_request_.protected_auction_ciphertext());

  EXPECT_NE(actual_response.seller_component_ciphertexts().at(kSeller2),
            actual_response.seller_component_ciphertexts().at(kSeller1));
  DecryptAndTestEncodedText(actual_response, kSeller1);
  DecryptAndTestEncodedText(actual_response, kSeller2);
}

TEST_F(GetComponentAuctionCiphertextsReactorTest,
       ReturnsEmptyOutputForEmptySellerList) {
  valid_request_.clear_component_sellers();

  GetComponentAuctionCiphertextsResponse expected_response;
  auto actual_response = CreateReactorAndRun(valid_request_);
  EXPECT_THAT(actual_response, EqualsProto(expected_response));
}

TEST_F(GetComponentAuctionCiphertextsReactorTest,
       ReturnsEmptyOutputForEmptyCiphertext) {
  valid_request_.clear_protected_auction_ciphertext();

  GetComponentAuctionCiphertextsResponse expected_response;
  auto actual_response = CreateReactorAndRun(valid_request_);
  EXPECT_THAT(actual_response, EqualsProto(expected_response));
}

TEST_F(GetComponentAuctionCiphertextsReactorTest, IgnoresUnknownSeller) {
  valid_request_.add_component_sellers(kUnknownSeller);
  auto actual_response = CreateReactorAndRun(valid_request_);
  ASSERT_EQ(actual_response.seller_component_ciphertexts().size(), 2);
  DecryptAndTestEncodedText(actual_response, kSeller1);
  DecryptAndTestEncodedText(actual_response, kSeller2);
}

}  // namespace privacy_sandbox::bidding_auction_servers
