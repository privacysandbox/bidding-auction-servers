// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/encryption/crypto_client_wrapper.h"

#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <utility>

#include <google/protobuf/util/message_differencer.h>
#include <include/gmock/gmock-actions.h>

#include "absl/strings/escaping.h"
#include "gtest/gtest.h"
#include "proto/hpke.pb.h"
#include "proto/tink.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::cmrt::sdk::crypto_service::v1::AeadDecryptRequest;
using ::google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse;
using ::google::cmrt::sdk::crypto_service::v1::AeadEncryptedData;
using ::google::cmrt::sdk::crypto_service::v1::AeadEncryptRequest;
using ::google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse;
using ::google::cmrt::sdk::crypto_service::v1::HpkeDecryptRequest;
using ::google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse;
using ::google::cmrt::sdk::crypto_service::v1::HpkeEncryptedData;
using ::google::cmrt::sdk::crypto_service::v1::HpkeEncryptRequest;
using ::google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse;
using ::google::scp::cpio::Callback;

using ::google::cmrt::sdk::public_key_service::v1::PublicKey;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::SuccessExecutionResult;

inline constexpr char kPlaintext[] = "plaintext";
inline constexpr char kCiphertext[] = "ciphertext";

// SCP's MockCryptoClientProvider isn't available outside of their repo.
class MockCryptoClientProvider
    : public google::scp::cpio::CryptoClientInterface {
 public:
  absl::Status init_result_mock = absl::OkStatus();
  absl::Status Init() noexcept override { return init_result_mock; }

  absl::Status run_result_mock = absl::OkStatus();
  absl::Status Run() noexcept override { return run_result_mock; }

  absl::Status stop_result_mock = absl::OkStatus();
  absl::Status Stop() noexcept override { return stop_result_mock; }

  // NOLINTNEXTLINE
  MOCK_METHOD(absl::Status, HpkeEncrypt,
              (HpkeEncryptRequest request,
               Callback<HpkeEncryptResponse> callback),
              (override, noexcept));
  // NOLINTNEXTLINE
  MOCK_METHOD(absl::Status, HpkeDecrypt,
              (HpkeDecryptRequest request,
               Callback<HpkeDecryptResponse> callback),
              (override, noexcept));
  // NOLINTNEXTLINE
  MOCK_METHOD(absl::Status, AeadEncrypt,
              (AeadEncryptRequest request,
               Callback<AeadEncryptResponse> callback),
              (override, noexcept));
  // NOLINTNEXTLINE
  MOCK_METHOD(absl::Status, AeadDecrypt,
              (AeadDecryptRequest request,
               Callback<AeadDecryptResponse> callback),
              (override, noexcept));
};

TEST(CryptoClientWrapperTest, HpkeEncrypt_Success) {
  PublicKey public_key;
  public_key.set_key_id("key_id");
  public_key.set_public_key("public_key");

  HpkeEncryptRequest expected_request;
  google::cmrt::sdk::public_key_service::v1::PublicKey key;
  key.set_key_id(public_key.key_id());
  key.set_public_key(public_key.public_key());
  *expected_request.mutable_public_key() = std::move(key);
  expected_request.set_payload(kPlaintext);
  expected_request.set_shared_info(kSharedInfo);
  expected_request.set_is_bidirectional(true);
  expected_request.set_secret_length(
      google::cmrt::sdk::crypto_service::v1::SECRET_LENGTH_32_BYTES);

  HpkeEncryptResponse mock_response;
  HpkeEncryptedData data;
  data.set_ciphertext(kCiphertext);
  *mock_response.mutable_encrypted_data() = data;
  mock_response.set_secret("secret");

  std::unique_ptr<MockCryptoClientProvider> mock_crypto_client =
      std::make_unique<MockCryptoClientProvider>();
  EXPECT_CALL(*mock_crypto_client, HpkeEncrypt)
      .WillOnce([&expected_request, &mock_response](
                    const HpkeEncryptRequest& request,
                    const Callback<HpkeEncryptResponse>& callback) {
        EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
            request, expected_request));
        callback(SuccessExecutionResult(), mock_response);
        return absl::OkStatus();
      });
  CryptoClientWrapper crypto_client(std::move(mock_crypto_client));

  absl::StatusOr<HpkeEncryptResponse> actual_response =
      crypto_client.HpkeEncrypt(public_key, kPlaintext);
  ASSERT_TRUE(actual_response.ok());
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *actual_response, mock_response));
}

TEST(CryptoClientWrapperTest, HpkeEncrypt_Failure) {
  std::unique_ptr<MockCryptoClientProvider> mock_crypto_client =
      std::make_unique<MockCryptoClientProvider>();
  EXPECT_CALL(*mock_crypto_client, HpkeEncrypt)
      .WillOnce([](const HpkeEncryptRequest& request,
                   const Callback<HpkeEncryptResponse>& callback) {
        callback(FailureExecutionResult(0), HpkeEncryptResponse());
        return absl::OkStatus();
      });
  CryptoClientWrapper crypto_client(std::move(mock_crypto_client));

  PublicKey public_key;
  public_key.set_key_id("key_id");
  public_key.set_public_key("public_key");

  absl::StatusOr<HpkeEncryptResponse> actual_response =
      crypto_client.HpkeEncrypt(public_key, "plaintext_payload");
  EXPECT_FALSE(actual_response.ok());
}

TEST(CryptoClientWrapperTest, HpkeDecrypt_Success) {
  server_common::PrivateKey private_key;
  private_key.key_id = "keyid";
  private_key.private_key = "privatekey";

  google::crypto::tink::HpkePrivateKey hpke_private_key;
  hpke_private_key.set_private_key(private_key.private_key);

  const int unused_key_id = 0;
  google::crypto::tink::Keyset keyset;
  keyset.set_primary_key_id(unused_key_id);
  keyset.add_key();
  keyset.mutable_key(0)->set_key_id(unused_key_id);
  keyset.mutable_key(0)->mutable_key_data()->set_value(
      hpke_private_key.SerializeAsString());

  HpkeDecryptRequest expected_request;
  google::cmrt::sdk::private_key_service::v1::PrivateKey key;
  key.set_key_id(private_key.key_id);
  key.set_private_key(absl::Base64Escape(keyset.SerializeAsString()));
  *expected_request.mutable_private_key() = std::move(key);
  HpkeEncryptedData data;
  data.set_ciphertext(kCiphertext);
  *expected_request.mutable_encrypted_data() = std::move(data);
  expected_request.set_shared_info(kSharedInfo);
  expected_request.set_is_bidirectional(true);
  expected_request.set_secret_length(
      google::cmrt::sdk::crypto_service::v1::SECRET_LENGTH_32_BYTES);

  HpkeDecryptResponse mock_response;
  mock_response.set_payload(kPlaintext);
  mock_response.set_secret("secret");

  std::unique_ptr<MockCryptoClientProvider> mock_crypto_client =
      std::make_unique<MockCryptoClientProvider>();
  EXPECT_CALL(*mock_crypto_client, HpkeDecrypt)
      .WillOnce([&expected_request, &mock_response](
                    const HpkeDecryptRequest& request,
                    const Callback<HpkeDecryptResponse>& callback) {
        EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
            request, expected_request));
        callback(SuccessExecutionResult(), mock_response);
        return absl::OkStatus();
      });
  CryptoClientWrapper crypto_client(std::move(mock_crypto_client));

  absl::StatusOr<HpkeDecryptResponse> actual_response =
      crypto_client.HpkeDecrypt(private_key, kCiphertext);
  ASSERT_TRUE(actual_response.ok());

  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *actual_response, mock_response));
}

TEST(CryptoClientWrapperTest, AeadEncrypt_Success) {
  const std::string secret = "secret";

  AeadEncryptRequest expected_request;
  expected_request.set_payload(kPlaintext);
  expected_request.set_secret(secret);
  expected_request.set_shared_info(kSharedInfo);

  AeadEncryptResponse mock_response;
  AeadEncryptedData data;
  data.set_ciphertext(kCiphertext);

  std::unique_ptr<MockCryptoClientProvider> mock_crypto_client =
      std::make_unique<MockCryptoClientProvider>();
  EXPECT_CALL(*mock_crypto_client, AeadEncrypt)
      .WillOnce([&expected_request, &mock_response](
                    const AeadEncryptRequest& request,
                    const Callback<AeadEncryptResponse>& callback) {
        EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
            request, expected_request));
        callback(SuccessExecutionResult(), mock_response);
        return absl::OkStatus();
      });
  CryptoClientWrapper crypto_client(std::move(mock_crypto_client));

  absl::StatusOr<AeadEncryptResponse> actual_response =
      crypto_client.AeadEncrypt(kPlaintext, secret);
  ASSERT_TRUE(actual_response.ok());

  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *actual_response, mock_response));
}

TEST(CryptoClientWrapperTest, AeadDecrypt_Success) {
  const std::string secret = "secret";

  AeadDecryptRequest expected_request;
  AeadEncryptedData data;
  data.set_ciphertext(kCiphertext);
  *expected_request.mutable_encrypted_data() = std::move(data);
  expected_request.set_secret(secret);
  expected_request.set_shared_info(kSharedInfo);

  AeadDecryptResponse mock_response;
  mock_response.set_payload(kPlaintext);

  std::unique_ptr<MockCryptoClientProvider> mock_crypto_client =
      std::make_unique<MockCryptoClientProvider>();
  EXPECT_CALL(*mock_crypto_client, AeadDecrypt)
      .WillOnce([&expected_request, &mock_response](
                    const AeadDecryptRequest& request,
                    const Callback<AeadDecryptResponse>& callback) {
        EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
            request, expected_request));
        callback(SuccessExecutionResult(), mock_response);
        return absl::OkStatus();
      });
  CryptoClientWrapper crypto_client(std::move(mock_crypto_client));

  absl::StatusOr<AeadDecryptResponse> actual_response =
      crypto_client.AeadDecrypt(kCiphertext, secret);
  ASSERT_TRUE(actual_response.ok());

  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *actual_response, mock_response));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
