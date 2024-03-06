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

#include "gtest/gtest.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/cpp/communication/encoding_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

std::unique_ptr<server_common::MockKeyFetcherManager>
MockKeyFetcherReturningDefaultPrivateKey() {
  auto key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  server_common::PrivateKey private_key;
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .Times(1)
      .WillOnce([](const google::scp::cpio::PublicPrivateKeyPairId& key_id) {
        // Verify key id is sent as is.
        EXPECT_EQ(key_id, std::to_string(HpkeKeyset{}.key_id));
        // Return default private key from test utils.
        return GetPrivateKey();
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

  auto deframed_req = server_common::DecodeRequestPayload((*output)->plaintext);
  ASSERT_TRUE(deframed_req.ok()) << deframed_req.status();
  EXPECT_EQ(deframed_req->compressed_data,
            protected_auction_input.SerializeAsString());
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
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
