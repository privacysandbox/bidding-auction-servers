//   Copyright 2024 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include "services/seller_frontend_service/util/encryption_util.h"

#include <utility>

#include "gtest/gtest.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/encryption/key_fetcher/fake_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

std::unique_ptr<server_common::MockKeyFetcherManager>
MockKeyFetcherReturningDefaultPrivateKey() {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .Times(1)
      .WillOnce([](const google::scp::cpio::PublicPrivateKeyPairId& key_id) {
        // Returns test mode keys
        server_common::FakeKeyFetcherManager fake_key_fetcher_manager;
        auto key = fake_key_fetcher_manager.GetPrivateKey(key_id);
        // Verify key id is sent as is.
        EXPECT_EQ(key_id, key->key_id);
        // Return default private key from test utils.
        return key;
      });
  return key_fetcher_manager;
}

std::unique_ptr<server_common::MockKeyFetcherManager>
MockKeyFetcherReturningPublicAndPrivateKey(
    const server_common::CloudPlatform& expected_cp) {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPublicKey)
      .Times(1)
      .WillOnce(
          [expected_cp](const server_common::CloudPlatform& cloud_platform) {
            // Verify cloud platform is sent as is.
            EXPECT_EQ(expected_cp, cloud_platform);
            // Returns test mode keys
            server_common::FakeKeyFetcherManager fake_key_fetcher_manager;
            return fake_key_fetcher_manager.GetPublicKey(cloud_platform);
          });
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .Times(1)
      .WillOnce([](const google::scp::cpio::PublicPrivateKeyPairId& key_id) {
        // Returns test mode keys
        server_common::FakeKeyFetcherManager fake_key_fetcher_manager;
        return fake_key_fetcher_manager.GetPrivateKey(key_id);
      });
  return key_fetcher_manager;
}

std::unique_ptr<server_common::MockKeyFetcherManager>
MockKeyFetcherReturningNoPrivateKey() {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  server_common::PrivateKey private_key;
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .Times(1)
      .WillOnce([](const google::scp::cpio::PublicPrivateKeyPairId& key_id) {
        return std::nullopt;
      });
  return key_fetcher_manager;
}

void DecodeAndTestProtectedAuctionInput(
    std::string& plaintext,
    const ProtectedAuctionInput& protected_auction_input) {
  auto deframed_req = server_common::DecodeRequestPayload(plaintext);
  ASSERT_TRUE(deframed_req.ok()) << deframed_req.status();
  EXPECT_EQ(deframed_req->compressed_data,
            protected_auction_input.SerializeAsString());
}

TEST(DecryptOHTTPEncapsulatedHpkeCiphertextTest, DecryptsRequestSuccessfully) {
  auto key_fetcher_manager = MockKeyFetcherReturningDefaultPrivateKey();
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(CLIENT_TYPE_ANDROID, "");
  absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>> output =
      DecryptOHTTPEncapsulatedHpkeCiphertext(
          request.protected_auction_ciphertext(), *key_fetcher_manager);
  ASSERT_TRUE(output.ok()) << output.status();
  // Default private key returned by key fetcher manager.
  auto expected_private_key = GetPrivateKey();
  EXPECT_EQ((*output)->private_key.key_id, expected_private_key.key_id);
  EXPECT_EQ((*output)->private_key.private_key,
            expected_private_key.private_key);

  DecodeAndTestProtectedAuctionInput((*output)->plaintext,
                                     protected_auction_input);
}

TEST(DecryptOHTTPEncapsulatedHpkeCiphertextTest,
     ReturnsErrorForBadEncapsulation) {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  std::string input = "invalid ciphertext";
  absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>> output =
      DecryptOHTTPEncapsulatedHpkeCiphertext(input, *key_fetcher_manager);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(DecryptOHTTPEncapsulatedHpkeCiphertextTest,
     ReturnsErrorFromKeyFetcherManager) {
  auto key_fetcher_manager = MockKeyFetcherReturningNoPrivateKey();
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(CLIENT_TYPE_ANDROID, "");
  absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>> output =
      DecryptOHTTPEncapsulatedHpkeCiphertext(
          request.protected_auction_ciphertext(), *key_fetcher_manager);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kNotFound);
}

TEST(HpkeEncryptAndOHTTPEncapsulateTest, EncryptsString_WithBhttpLabel) {
  auto key_fetcher_manager = MockKeyFetcherReturningPublicAndPrivateKey(
      server_common::CloudPlatform::kGcp);
  std::string input = "random-string";
  auto output = HpkeEncryptAndOHTTPEncapsulate(
      input, quiche::ObliviousHttpHeaderKeyConfig::kOhttpRequestLabel,
      *key_fetcher_manager, server_common::CloudPlatform::kGcp);
  ASSERT_TRUE(output.ok()) << output.status();

  // Decrypt and test equality with input object.
  absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>> decrypted_output =
      DecryptOHTTPEncapsulatedHpkeCiphertext(output->ciphertext,
                                             *key_fetcher_manager);
  ASSERT_TRUE(decrypted_output.ok()) << decrypted_output.status();
  EXPECT_EQ((*decrypted_output)->plaintext, input);
}

TEST(HpkeEncryptAndOHTTPEncapsulateTest,
     FailesStringEncryption_WithRandomLabel) {
  std::string input = "random-string";
  std::string label = "message/random label";
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  auto output = HpkeEncryptAndOHTTPEncapsulate(
      input, label, *key_fetcher_manager, server_common::CloudPlatform::kGcp);
  ASSERT_FALSE(output.ok());
}

std::pair<std::string, ProtectedAuctionInput> GetProtectedAuctionInput() {
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(CLIENT_TYPE_ANDROID, "");
  std::string encoded_request = protected_auction_input.SerializeAsString();
  absl::StatusOr<std::string> framed_request =
      server_common::EncodeResponsePayload(
          server_common::CompressionType::kGzip, encoded_request,
          GetEncodedDataSize(encoded_request.size()));
  EXPECT_TRUE(framed_request.ok()) << framed_request.status();
  return {std::move(*framed_request), std::move(protected_auction_input)};
}

TEST(HpkeEncryptAndOHTTPEncapsulateTest,
     EncryptsProtectedAuctionInput_WithBiddingAuctionOhttpLabel_ForGcp) {
  auto key_fetcher_manager = MockKeyFetcherReturningPublicAndPrivateKey(
      server_common::CloudPlatform::kGcp);
  auto [framed_request, protected_auction_input] = GetProtectedAuctionInput();

  auto output = HpkeEncryptAndOHTTPEncapsulate(
      framed_request, kBiddingAuctionOhttpRequestLabel, *key_fetcher_manager,
      server_common::CloudPlatform::kGcp);
  ASSERT_TRUE(output.ok()) << output.status();

  // Decrypt and test equality with input object.
  absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>> decrypted_output =
      DecryptOHTTPEncapsulatedHpkeCiphertext(output->ciphertext,
                                             *key_fetcher_manager);
  ASSERT_TRUE(decrypted_output.ok()) << decrypted_output.status();
  DecodeAndTestProtectedAuctionInput((*decrypted_output)->plaintext,
                                     protected_auction_input);
}

TEST(HpkeEncryptAndOHTTPEncapsulateTest,
     EncryptsProtectedAuctionInput_WithBiddingAuctionOhttpLabel_ForAws) {
  auto key_fetcher_manager = MockKeyFetcherReturningPublicAndPrivateKey(
      server_common::CloudPlatform::kAws);
  auto [framed_request, protected_auction_input] = GetProtectedAuctionInput();

  auto output = HpkeEncryptAndOHTTPEncapsulate(
      framed_request, kBiddingAuctionOhttpRequestLabel, *key_fetcher_manager,
      server_common::CloudPlatform::kAws);
  ASSERT_TRUE(output.ok()) << output.status();

  // Decrypt and test equality with input object.
  absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>> decrypted_output =
      DecryptOHTTPEncapsulatedHpkeCiphertext(output->ciphertext,
                                             *key_fetcher_manager);
  ASSERT_TRUE(decrypted_output.ok()) << decrypted_output.status();
  DecodeAndTestProtectedAuctionInput((*decrypted_output)->plaintext,
                                     protected_auction_input);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
