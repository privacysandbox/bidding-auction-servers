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

#include "services/common/util/hpke_utils.h"

#include <memory>

#include "gtest/gtest.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kPlaintext[] = "plaintext";
constexpr char kCiphertext[] = "ciphertext";
constexpr char kSecret[] = "secret";
constexpr char kKeyId[] = "keyid";
constexpr char kPublicKey[] = "publickey";
constexpr char kPrivateKey[] = "publickey";

google::cmrt::sdk::public_key_service::v1::PublicKey MockPublicKey() {
  google::cmrt::sdk::public_key_service::v1::PublicKey public_key;
  public_key.set_key_id(kKeyId);
  public_key.set_public_key(kPublicKey);
  return public_key;
}

server_common::PrivateKey MockPrivateKey() {
  server_common::PrivateKey private_key;
  private_key.key_id = kKeyId;
  private_key.private_key = kPrivateKey;
  return private_key;
}

std::unique_ptr<server_common::MockKeyFetcherManager>
MockKeyFetcherReturningPublicKey(
    const server_common::CloudPlatform& expected_cp) {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPublicKey)
      .Times(1)
      .WillOnce(
          [expected_cp](const server_common::CloudPlatform& cloud_platform) {
            // Verify cloud platform is sent as is.
            EXPECT_EQ(expected_cp, cloud_platform);
            return MockPublicKey();
          });
  return key_fetcher_manager;
}

std::unique_ptr<server_common::MockKeyFetcherManager>
MockKeyFetcherReturningPrivateKey(absl::string_view expected_key_id) {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  server_common::PrivateKey private_key;
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .Times(1)
      .WillOnce([expected_key_id = expected_key_id.data()](
                    const google::scp::cpio::PublicPrivateKeyPairId& key_id) {
        // Verify key id is sent as is.
        EXPECT_EQ(expected_key_id, key_id);
        return MockPrivateKey();
      });
  return key_fetcher_manager;
}

std::unique_ptr<server_common::MockKeyFetcherManager>
MockKeyFetcherReturningEmptyPublicKey() {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPublicKey)
      .Times(1)
      .WillOnce(testing::Return(absl::InternalError("Error")));
  return key_fetcher_manager;
}

std::unique_ptr<server_common::MockKeyFetcherManager>
MockKeyFetcherReturningEmptyPrivateKey() {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .Times(1)
      .WillOnce(testing::Return(std::nullopt));
  return key_fetcher_manager;
}

std::unique_ptr<MockCryptoClientWrapper> MockCryptoClientWithHpkeEncrypt(
    absl::string_view plaintext) {
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  // Mock the HpkeEncrypt() call on the crypto_client_.
  EXPECT_CALL(*crypto_client, HpkeEncrypt)
      .Times(1)
      .WillOnce(
          [plaintext = plaintext.data(), public_key = MockPublicKey()](
              const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
              const std::string& plaintext_payload) {
            // Verify that the key received from key fetcher manager is sent.
            EXPECT_EQ(key.key_id(), public_key.key_id());
            EXPECT_EQ(key.public_key(), public_key.public_key());
            EXPECT_EQ(plaintext_payload, plaintext);
            google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
                hpke_encrypt_response;
            hpke_encrypt_response.set_secret(kSecret);
            hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kKeyId);
            hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
                kCiphertext);
            return hpke_encrypt_response;
          });
  return crypto_client;
}

std::unique_ptr<MockCryptoClientWrapper> MockCryptoClientWithHpkeDecrypt(
    absl::string_view ciphertext) {
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  // Mock the HpkeEncrypt() call on the crypto_client_.
  EXPECT_CALL(*crypto_client, HpkeDecrypt)
      .Times(1)
      .WillOnce(
          [ciphertext = ciphertext.data(), private_key = MockPrivateKey()](
              const server_common::PrivateKey& key,
              const std::string& ciphertext_payload) {
            // Verify that the key received from key fetcher manager is sent.
            EXPECT_EQ(key.key_id, private_key.key_id);
            EXPECT_EQ(key.private_key, private_key.private_key);
            EXPECT_EQ(ciphertext_payload, ciphertext);
            // Mock the HpkeDecrypt() call on the crypto client.
            google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
                hpke_decrypt_response;
            hpke_decrypt_response.set_payload(kPlaintext);
            return hpke_decrypt_response;
          });
  return crypto_client;
}

std::unique_ptr<MockCryptoClientWrapper>
MockCryptoClientWithNoHpkeEncryptCall() {
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  EXPECT_CALL(*crypto_client, HpkeEncrypt).Times(0);
  return crypto_client;
}

std::unique_ptr<MockCryptoClientWrapper>
MockCryptoClientWithHpkeEncryptError() {
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  EXPECT_CALL(*crypto_client, HpkeEncrypt)
      .Times(1)
      .WillOnce(testing::Return(absl::InternalError("Error")));
  return crypto_client;
}

std::unique_ptr<MockCryptoClientWrapper>
MockCryptoClientWithNoHpkeDecryptCall() {
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  EXPECT_CALL(*crypto_client, HpkeDecrypt).Times(0);
  return crypto_client;
}

std::unique_ptr<MockCryptoClientWrapper>
MockCryptoClientWithHpkeDecryptError() {
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  EXPECT_CALL(*crypto_client, HpkeDecrypt)
      .Times(1)
      .WillOnce(testing::Return(absl::InternalError("Error")));
  return crypto_client;
}

TEST(HpkeEncryptTest, CallsCryptoClientAndKeyFetcherManagerForGcp) {
  auto crypto_client = MockCryptoClientWithHpkeEncrypt(kPlaintext);
  auto key_fetcher_manager =
      MockKeyFetcherReturningPublicKey(server_common::CloudPlatform::kGcp);
  absl::StatusOr<HpkeMessage> output =
      HpkeEncrypt(kPlaintext, *crypto_client, *key_fetcher_manager,
                  server_common::CloudPlatform::kGcp);
  ASSERT_TRUE(output.ok());
  EXPECT_EQ(output->ciphertext, kCiphertext);
  EXPECT_EQ(output->key_id, kKeyId);
  EXPECT_EQ(output->secret, kSecret);
}

TEST(HpkeEncryptTest, CallsCryptoClientAndKeyFetcherManagerForAws) {
  auto crypto_client = MockCryptoClientWithHpkeEncrypt(kPlaintext);
  auto key_fetcher_manager =
      MockKeyFetcherReturningPublicKey(server_common::CloudPlatform::kAws);
  absl::StatusOr<HpkeMessage> output =
      HpkeEncrypt(kPlaintext, *crypto_client, *key_fetcher_manager,
                  server_common::CloudPlatform::kAws);
  ASSERT_TRUE(output.ok());
  EXPECT_EQ(output->ciphertext, kCiphertext);
  EXPECT_EQ(output->key_id, kKeyId);
  EXPECT_EQ(output->secret, kSecret);
}

TEST(HpkeEncryptTest, ReturnsErrorFromKeyFetcher) {
  auto crypto_client = MockCryptoClientWithNoHpkeEncryptCall();
  auto key_fetcher_manager = MockKeyFetcherReturningEmptyPublicKey();
  absl::StatusOr<HpkeMessage> output =
      HpkeEncrypt(kPlaintext, *crypto_client, *key_fetcher_manager,
                  server_common::CloudPlatform::kGcp);
  ASSERT_FALSE(output.ok());
}

TEST(HpkeEncryptTest, ReturnsErrorFromCryptoClient) {
  auto crypto_client = MockCryptoClientWithHpkeEncryptError();
  auto key_fetcher_manager =
      MockKeyFetcherReturningPublicKey(server_common::CloudPlatform::kAws);
  absl::StatusOr<HpkeMessage> output =
      HpkeEncrypt(kPlaintext, *crypto_client, *key_fetcher_manager,
                  server_common::CloudPlatform::kAws);
  ASSERT_FALSE(output.ok());
}

TEST(HpkeDecryptTest, CallsCryptoClientAndKeyFetcherManager) {
  auto crypto_client = MockCryptoClientWithHpkeDecrypt(kCiphertext);
  auto key_fetcher_manager = MockKeyFetcherReturningPrivateKey(kKeyId);
  absl::StatusOr<std::string> output =
      HpkeDecrypt(kCiphertext, kKeyId, *crypto_client, *key_fetcher_manager);
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(*output, kPlaintext);
}

TEST(HpkeDecryptTest, ReturnsErrorFromKeyFetcher) {
  auto crypto_client = MockCryptoClientWithNoHpkeDecryptCall();
  auto key_fetcher_manager = MockKeyFetcherReturningEmptyPrivateKey();
  absl::StatusOr<std::string> output =
      HpkeDecrypt(kCiphertext, kKeyId, *crypto_client, *key_fetcher_manager);
  ASSERT_FALSE(output.ok());
}

TEST(HpkeDecryptTest, ReturnsErrorFromCryptoClient) {
  auto crypto_client = MockCryptoClientWithHpkeDecryptError();
  auto key_fetcher_manager = MockKeyFetcherReturningPrivateKey(kKeyId);
  absl::StatusOr<std::string> output =
      HpkeDecrypt(kCiphertext, kKeyId, *crypto_client, *key_fetcher_manager);
  ASSERT_FALSE(output.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
