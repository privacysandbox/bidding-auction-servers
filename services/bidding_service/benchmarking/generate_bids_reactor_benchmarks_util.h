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
#ifndef SERVICES_BIDDING_SERVICE_BENCHMARKING_GENERATE_BIDS_REACTOR_BENCHMARKS_UTIL_H_
#define SERVICES_BIDDING_SERVICE_BENCHMARKING_GENERATE_BIDS_REACTOR_BENCHMARKS_UTIL_H_

#include <string>
#include <utility>

#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kTestSecret[] = "secret";
constexpr char kTestKeyId[] = "keyid";
constexpr char kTestInterestGroupNameTemplate[] = "test_interest_group_%d";
constexpr char kTrustedBiddingSignalsKey[] = "trusted_bidding_signals_key";
constexpr char kStructKeyTemplate[] = "structKey_%d";
constexpr char kStructValueTemplate[] = "structValue_%d";
constexpr int kNumInterestGroupsForBidding = 50;
constexpr int kNumAdRenderIds = 20;
constexpr int kNumSignalsFields = 20;

using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;

class CryptoClientStub : public CryptoClientWrapperInterface {
 public:
  explicit CryptoClientStub(RawRequest* raw_request)
      : raw_request_(*raw_request) {}
  virtual ~CryptoClientStub() = default;

  // Decrypts a ciphertext using HPKE.
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse>
  HpkeDecrypt(const server_common::PrivateKey& private_key,
              const std::string& ciphertext) noexcept override;

  // Encrypts a plaintext payload using HPKE and the provided public key.
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse>
  HpkeEncrypt(const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
              const std::string& plaintext_payload) noexcept override;

  // Encrypts plaintext payload using AEAD and a secret derived from the HPKE
  // decrypt operation.
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
  AeadEncrypt(const std::string& plaintext_payload,
              const std::string& secret) noexcept override;

  // Decrypts a ciphertext using AEAD and a secret derived from the HPKE
  // encrypt operation.
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
  AeadDecrypt(const std::string& ciphertext,
              const std::string& secret) noexcept override;

 protected:
  const RawRequest& raw_request_;
};

absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse>
CryptoClientStub::HpkeDecrypt(const server_common::PrivateKey& private_key,
                              const std::string& ciphertext) noexcept {
  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
      hpke_decrypt_response;
  hpke_decrypt_response.set_payload(raw_request_.SerializeAsString());
  hpke_decrypt_response.set_secret(kTestSecret);
  return hpke_decrypt_response;
}

absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse>
CryptoClientStub::HpkeEncrypt(
    const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
    const std::string& plaintext_payload) noexcept {
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
      hpke_encrypt_response;
  hpke_encrypt_response.set_secret(kTestSecret);
  hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kTestKeyId);
  hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
      raw_request_.SerializeAsString());
  return hpke_encrypt_response;
}

absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
CryptoClientStub::AeadEncrypt(const std::string& plaintext_payload,
                              const std::string& secret) noexcept {
  google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
  data.set_ciphertext(plaintext_payload);
  google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
      aead_encrypt_response;
  *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
  return aead_encrypt_response;
}

absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
CryptoClientStub::AeadDecrypt(const std::string& ciphertext,
                              const std::string& secret) noexcept {
  google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse
      aead_decrypt_response;
  aead_decrypt_response.set_payload(raw_request_.SerializeAsString());
  return aead_decrypt_response;
}

InterestGroupForBidding GetInterestGroupForBidding(const std::string& name) {
  InterestGroupForBidding ig_for_bidding;
  ig_for_bidding.set_name(name);
  ig_for_bidding.mutable_trusted_bidding_signals_keys()->Add(
      kTrustedBiddingSignalsKey);
  ig_for_bidding.set_trusted_bidding_signals(
      MakeTrustedBiddingSignalsForIG(ig_for_bidding));
  ig_for_bidding.set_user_bidding_signals(
      R"JSON({"years": [1776, 1868], "name": "winston", "someId": 1789})JSON");
  for (int i = 0; i < kNumAdRenderIds; ++i) {
    *ig_for_bidding.mutable_ad_render_ids()->Add() =
        absl::StrCat("ad_render_id_", i);
  }
  ig_for_bidding.mutable_android_signals();
  return ig_for_bidding;
}

// These are borrowed from the random module and randomness has been removed
// since we want to run the load deterministically.
google::protobuf::Struct MakeAJsonStruct(int num_fields) {
  google::protobuf::Struct out_struct;
  auto& struct_fields = *out_struct.mutable_fields();
  for (int i = 0; i < num_fields; ++i) {
    struct_fields[absl::StrFormat(kStructKeyTemplate, i)].set_string_value(
        absl::StrFormat(kStructValueTemplate, i));
  }
  return out_struct;
}

std::string MakeAStructJsonString(int num_fields = kNumSignalsFields) {
  std::string json_output;
  auto fields = MakeAJsonStruct(num_fields);
  CHECK_OK(ProtoToJson(fields, &json_output));
  return json_output;
}

GenerateBidsRequest::GenerateBidsRawRequest
GetGenerateBidsRawRequestForAndroid() {
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  for (int i = 0; i < kNumInterestGroupsForBidding; ++i) {
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        GetInterestGroupForBidding(
            absl::StrFormat(kTestInterestGroupNameTemplate, i));
  }
  std::string signals = MakeAStructJsonString();
  raw_request.set_auction_signals(signals);
  raw_request.set_buyer_signals(std::move(signals));
  return raw_request;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BENCHMARKING_GENERATE_BIDS_REACTOR_BENCHMARKS_UTIL_H_
