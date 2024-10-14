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

#include "tools/secure_invoke/payload_generator/payload_packaging_utils.h"

#include <algorithm>
#include <ctime>
#include <memory>
#include <vector>

#include <google/protobuf/util/message_differencer.h>

#include "include/gmock/gmock.h"
#include "include/gtest/gtest.h"
#include "services/common/compression/gzip.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/test/random.h"
#include "services/common/util/hpke_utils.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/proto_mapping_util.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/communication/ohttp_utils.h"
#include "src/encryption/key_fetcher/fake_key_fetcher_manager.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

server_common::PrivateKey GetPrivateKey() {
  HpkeKeyset default_keyset;
  server_common::PrivateKey private_key;
  private_key.key_id = std::to_string(default_keyset.key_id);
  private_key.private_key = GetHpkePrivateKey(default_keyset.private_key);
  return private_key;
}

// This test follows the exact steps for decoding and decryption for the
// protected auction input payload in a SelectAdRequest as followed in
// the SFE service for a request from a browser type client.
// These steps must always remain in sync with the SelectAdReactor.
// Therefore, it verifies that the ProtectedAuctionInput payload packaged
// by PackagePayloadForBrowser method is correctly parsed in the
// SelectAdReactor.
TEST(PackagePayloadForBrowserTest, GeneratesAValidBrowserPayload) {
  ProtectedAuctionInput expected =
      MakeARandomProtectedAuctionInput(CLIENT_TYPE_BROWSER);
  HpkeKeyset keyset;
  auto output = PackagePayload(expected, CLIENT_TYPE_BROWSER, keyset);
  ASSERT_TRUE(output.ok()) << output.status();

  absl::StatusOr<server_common::EncapsulatedRequest>
      parsed_encapsulated_request =
          server_common::ParseEncapsulatedRequest(output->first);
  ASSERT_TRUE(parsed_encapsulated_request.ok())
      << parsed_encapsulated_request.status();

  // Decrypt.
  auto decrypted_response = server_common::DecryptEncapsulatedRequest(
      GetPrivateKey(), *parsed_encapsulated_request);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
  ASSERT_TRUE(decoded_request.ok()) << decoded_request.status();

  // Decode.
  ErrorAccumulator error_accumulator;
  absl::StatusOr<ProtectedAuctionInput> actual = Decode<ProtectedAuctionInput>(
      decoded_request->compressed_data, error_accumulator);
  ASSERT_TRUE(actual.ok()) << actual.status();

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(*actual, expected)) << difference;
}

// This test follows the exact steps for decoding and decryption for the
// BuyerInput payload in a SelectAdRequest as followed in
// the SFE service for a request from a browser type client.
// These steps must always remain in sync with the SelectAdReactor.
// Therefore, it verifies that the BuyerInput payload packaged
// by PackageBuyerInputsForBrowser method is correctly parsed in the
// SelectAdReactor.
TEST(PackageBuyerInputsForBrowserTest, GeneratesAValidBuyerInputMap) {
  BuyerInput expected = MakeARandomBuyerInput();
  std::string owner = MakeARandomString();
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.insert({owner, expected});
  absl::StatusOr<google::protobuf::Map<std::string, std::string>> output =
      PackageBuyerInputsForBrowser(buyer_inputs);

  ASSERT_TRUE(output.ok()) << output.status();
  ASSERT_EQ(output->size(), 1);

  // Decompress and decode.
  const auto& buyer_input_it = output->find(owner);
  ASSERT_NE(buyer_input_it, output->end());
  ErrorAccumulator error_accumulator;
  absl::StatusOr<BuyerInput> actual =
      DecodeBuyerInput(owner, buyer_input_it->second, error_accumulator);
  ASSERT_TRUE(actual.ok()) << actual.status();

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(*actual, expected)) << difference;
}

// This test follows the exact steps for encoding and encryption for the
// AuctionResult payload as followed in the SFE service for a request
// from a browser type client. These steps must always remain in sync.
// Therefore, it verifies that the AuctionResult payload packaged by the
// SelectAdReactor is correctly parsed by UnpackageAuctionResult.
TEST(UnpackageBrowserAuctionResultTest, GeneratesAValidResponse) {
  AuctionResult expected = MakeARandomSingleSellerAuctionResult();

  ScoreAdsResponse::AdScore input;
  input.set_interest_group_owner(expected.interest_group_owner());
  input.set_interest_group_name(expected.interest_group_name());
  input.set_desirability(expected.score());
  input.set_render(expected.ad_render_url());

  // Encode.
  auto encoded_data =
      Encode(input, expected.bidding_groups(), expected.update_groups(),
             /*error=*/std::nullopt, [](const grpc::Status& status) {});
  ASSERT_TRUE(encoded_data.ok()) << encoded_data.status();

  // Compress.
  absl::string_view data_to_compress = absl::string_view(
      reinterpret_cast<char*>(encoded_data->data()), encoded_data->size());
  auto compressed_data = GzipCompress(data_to_compress);
  ASSERT_TRUE(compressed_data.ok()) << compressed_data.status();
  auto framed_data = server_common::EncodeResponsePayload(
      server_common::CompressionType::kGzip, *compressed_data,
      GetEncodedDataSize(compressed_data->size()));
  EXPECT_TRUE(framed_data.ok()) << framed_data.status();

  // Encrypt.
  HpkeKeyset keyset;
  auto input_ctxt_pair =
      PackagePayload(MakeARandomProtectedAuctionInput(CLIENT_TYPE_BROWSER),
                     CLIENT_TYPE_BROWSER, keyset);
  ASSERT_TRUE(input_ctxt_pair.ok()) << input_ctxt_pair.status();
  absl::StatusOr<std::string> encrypted_response =
      server_common::EncryptAndEncapsulateResponse(
          *std::move(framed_data), GetPrivateKey(), input_ctxt_pair->second,
          kBiddingAuctionOhttpRequestLabel);
  ASSERT_TRUE(encrypted_response.ok()) << encrypted_response.status();

  absl::StatusOr<AuctionResult> actual =
      UnpackageAuctionResult(*encrypted_response, CLIENT_TYPE_BROWSER,
                             input_ctxt_pair->second, keyset);
  ASSERT_TRUE(actual.ok()) << actual.status();

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(*actual, expected)) << difference;
}

TEST(PackagePayloadForAppTest, GeneratesAValidAppPayload) {
  HpkeKeyset keyset;
  ProtectedAuctionInput expected =
      MakeARandomProtectedAuctionInput(CLIENT_TYPE_ANDROID);
  absl::StatusOr<std::pair<std::string, quiche::ObliviousHttpRequest::Context>>
      output = PackagePayload(expected, CLIENT_TYPE_ANDROID, keyset);
  ASSERT_TRUE(output.ok()) << output.status();

  absl::StatusOr<server_common::EncapsulatedRequest>
      parsed_encapsulated_request =
          server_common::ParseEncapsulatedRequest(output->first);
  ASSERT_TRUE(parsed_encapsulated_request.ok())
      << parsed_encapsulated_request.status();

  // Decrypt.
  absl::StatusOr<quiche::ObliviousHttpRequest> decrypted_response =
      server_common::DecryptEncapsulatedRequest(GetPrivateKey(),
                                                *parsed_encapsulated_request);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Decode.
  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
  ASSERT_TRUE(decoded_request.ok()) << decoded_request.status();

  std::string payload = std::move(decoded_request->compressed_data);
  ProtectedAuctionInput actual;
  ASSERT_TRUE(actual.ParseFromArray(payload.data(), payload.size()))
      << "Failed to parse the input payload to proto";

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(actual, expected)) << difference;
}

TEST(PackageBuyerInputsForAppTest, GeneratesAValidBuyerInputMap) {
  BuyerInput expected = MakeARandomBuyerInput();
  std::string owner = MakeARandomString();
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.insert({owner, expected});
  absl::StatusOr<google::protobuf::Map<std::string, std::string>> output =
      PackageBuyerInputsForApp(buyer_inputs);

  ASSERT_TRUE(output.ok()) << output.status();
  ASSERT_EQ(output->size(), 1);

  // Decompress and decode.
  const auto& buyer_input_it = output->find(owner);
  ASSERT_NE(buyer_input_it, output->end());
  auto decompressed_buyer_input = GzipDecompress(buyer_input_it->second);
  ASSERT_TRUE(decompressed_buyer_input.ok())
      << decompressed_buyer_input.status();
  BuyerInput actual;
  ASSERT_TRUE(actual.ParseFromArray(decompressed_buyer_input->data(),
                                    decompressed_buyer_input->size()));

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(actual, expected)) << difference;
}

TEST(UnpackageAppAuctionResultTest, GeneratesAValidResponse) {
  AuctionResult expected = MakeARandomSingleSellerAuctionResult();

  // Encode, compress and frame with pre-amble.
  std::string serialized_result = expected.SerializeAsString();
  absl::StatusOr<std::string> compressed_data = GzipCompress(serialized_result);
  ASSERT_TRUE(compressed_data.ok()) << compressed_data.status();
  auto encoded_response = server_common::EncodeResponsePayload(
      server_common::CompressionType::kGzip, *compressed_data,
      GetEncodedDataSize(compressed_data->size()));
  ASSERT_TRUE(encoded_response.ok()) << encoded_response.status();

  // Encrypt.
  HpkeKeyset keyset;
  auto input_ctxt_pair =
      PackagePayload(MakeARandomProtectedAuctionInput(CLIENT_TYPE_ANDROID),
                     CLIENT_TYPE_ANDROID, keyset);
  ASSERT_TRUE(input_ctxt_pair.ok()) << input_ctxt_pair.status();
  absl::StatusOr<std::string> encrypted_response =
      server_common::EncryptAndEncapsulateResponse(
          *std::move(encoded_response), GetPrivateKey(),
          input_ctxt_pair->second, kBiddingAuctionOhttpRequestLabel);
  ASSERT_TRUE(encrypted_response.ok()) << encrypted_response.status();

  absl::StatusOr<AuctionResult> actual =
      UnpackageAuctionResult(*encrypted_response, CLIENT_TYPE_ANDROID,
                             input_ctxt_pair->second, keyset);
  ASSERT_TRUE(actual.ok()) << actual.status();

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(*actual, expected)) << difference;
}

TEST(PackageServerComponentAuctionResultTest, GeneratesAValidResponse) {
  AuctionResult expected = MakeARandomComponentAuctionResult(
      MakeARandomString(), MakeARandomString());
  HpkeKeyset hardcoded_keyset;
  auto output = PackageServerComponentAuctionResult(expected, hardcoded_keyset);
  ASSERT_TRUE(output.ok()) << output.status();
  SelectAdRequest::ComponentAuctionResult car;
  car.set_key_id(output->key_id);
  car.set_auction_result_ciphertext(output->ciphertext);

  // Decrypt.
  auto key_fetcher_manager =
      std::make_unique<server_common::FakeKeyFetcherManager>(
          hardcoded_keyset.public_key, hardcoded_keyset.private_key,
          std::to_string(hardcoded_keyset.key_id));
  auto crypto_client = CreateCryptoClient();
  auto actual = UnpackageServerAuctionComponentResult(car, *crypto_client,
                                                      *key_fetcher_manager);
  ASSERT_TRUE(actual.ok()) << actual.status();

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(*actual, expected)) << difference;
}

TEST(UnpackageResultForServerComponentAuctionTest, UnpacksPayloadSuccessfully) {
  AuctionResult expected = MakeARandomComponentAuctionResult(
      MakeARandomString(), MakeARandomString());
  // Encode
  std::string plaintext_response =
      FrameAndCompressProto(expected.SerializeAsString());
  // Encrypt
  HpkeKeyset hardcoded_keyset;
  auto key_fetcher_manager =
      std::make_unique<server_common::FakeKeyFetcherManager>(
          hardcoded_keyset.public_key, hardcoded_keyset.private_key,
          std::to_string(hardcoded_keyset.key_id));
  auto crypto_client = CreateCryptoClient();
  auto output_hpke =
      HpkeEncrypt(plaintext_response, *crypto_client, *key_fetcher_manager,
                  server_common::CloudPlatform::kGcp);
  ASSERT_TRUE(output_hpke.ok()) << output_hpke.status();
  ASSERT_EQ(output_hpke->key_id, std::to_string(hardcoded_keyset.key_id));

  auto actual = UnpackageResultForServerComponentAuction(
      output_hpke->ciphertext, output_hpke->key_id, hardcoded_keyset);
  ASSERT_TRUE(actual.ok()) << actual.status();

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(*actual, expected)) << difference;
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
