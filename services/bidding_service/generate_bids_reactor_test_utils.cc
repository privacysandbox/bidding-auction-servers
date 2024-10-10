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

#include "services/bidding_service/generate_bids_reactor_test_utils.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "google/protobuf/util/json_util.h"
#include "gtest/gtest.h"
#include "services/bidding_service/constants.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::protobuf::util::JsonStringToMessage;
using ::testing::AnyNumber;

PrivateAggregateContribution CreateTestPAggContribution(
    EventType event_type, absl::string_view event_name) {
  PrivateAggregateContribution contribution;
  contribution.mutable_event()->set_event_type(event_type);
  contribution.mutable_event()->set_event_name(event_name);
  return contribution;
}

void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client) {
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(AnyNumber())
      .WillRepeatedly(
          [](const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
             const std::string& plaintext_payload) {
            google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
                hpke_encrypt_response;
            hpke_encrypt_response.set_secret(kSecret);
            hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kKeyId);
            hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
                plaintext_payload);
            return hpke_encrypt_response;
          });

  // Mock the HpkeDecrypt() call on the crypto_client. This is used by the
  // service to decrypt the incoming request.
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const server_common::PrivateKey& private_key,
                         const std::string& ciphertext) {
        google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
            hpke_decrypt_response;
        *hpke_decrypt_response.mutable_payload() = ciphertext;
        hpke_decrypt_response.set_secret(kSecret);
        return hpke_decrypt_response;
      });

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(AnyNumber())
      .WillRepeatedly(
          [](const std::string& plaintext_payload, const std::string& secret) {
            google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
            *data.mutable_ciphertext() = plaintext_payload;
            google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
                aead_encrypt_response;
            *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
            return aead_encrypt_response;
          });
}

GenerateProtectedAppSignalsBidsRawRequest CreateRawProtectedAppSignalsRequest(
    const std::string& auction_signals, const std::string& buyer_signals,
    const ProtectedAppSignals& protected_app_signals, const std::string& seller,
    const std::string& publisher_name,
    absl::optional<ContextualProtectedAppSignalsData> contextual_pas_data,
    bool enable_unlimited_egress) {
  GenerateProtectedAppSignalsBidsRawRequest raw_request;
  raw_request.set_auction_signals(auction_signals);
  raw_request.set_buyer_signals(buyer_signals);
  *raw_request.mutable_protected_app_signals() = protected_app_signals;
  raw_request.set_seller(seller);
  raw_request.set_publisher_name(publisher_name);
  if (contextual_pas_data.has_value()) {
    *raw_request.mutable_contextual_protected_app_signals_data() =
        *std::move(contextual_pas_data);
  }
  raw_request.set_enable_unlimited_egress(enable_unlimited_egress);
  PS_LOG(INFO) << "Created request:\n" << raw_request.DebugString();
  return raw_request;
}

GenerateProtectedAppSignalsBidsRequest CreateProtectedAppSignalsRequest(
    const GenerateProtectedAppSignalsBidsRawRequest& raw_request) {
  GenerateProtectedAppSignalsBidsRequest request;
  request.set_request_ciphertext(raw_request.SerializeAsString());
  request.set_key_id(kKeyId);
  return request;
}

ProtectedAppSignals CreateProtectedAppSignals(
    const std::string& app_install_signals, int version) {
  ProtectedAppSignals protected_app_signals;
  protected_app_signals.set_encoding_version(version);
  *protected_app_signals.mutable_app_install_signals() = app_install_signals;
  return protected_app_signals;
}

absl::Status MockRomaExecution(std::vector<DispatchRequest>& batch,
                               BatchDispatchDoneCallback batch_callback,
                               absl::string_view expected_method_name,
                               absl::string_view expected_request_version,
                               const std::string& expected_json_response) {
  EXPECT_EQ(batch.size(), 1);
  const auto& request = batch[0];
  EXPECT_EQ(request.handler_name, expected_method_name);
  EXPECT_EQ(request.version_string, expected_request_version)
      << "Failed for: " << expected_method_name;

  std::vector<absl::StatusOr<DispatchResponse>> responses = {
      DispatchResponse({.id = request.id, .resp = expected_json_response})};
  batch_callback(responses);
  return absl::OkStatus();
}

std::string CreatePrepareDataForAdsRetrievalResponse(
    absl::string_view protected_app_signals,
    absl::string_view protected_embeddings) {
  return absl::Substitute(
      R"JSON(
      {
        "response": {
          "$0": "$1",
          "$2": "$3"
        }
      })JSON",
      kDecodedSignals, protected_app_signals, kRetrievalData,
      protected_embeddings);
}

std::string CreateGenerateBidsUdfResponse(
    absl::string_view render, double bid,
    absl::string_view egress_payload_string,
    absl::string_view debug_reporting_urls,
    absl::string_view temporary_egress_payload_string) {
  std::string base64_encoded_features_bytes;
  return absl::Substitute(R"JSON(
    {
      "render": "$0",
      "bid": $1,
      "egressPayload": "$2",
      "debugReportUrls": $3,
      "temporaryUnlimitedEgressPayload": "$4"
    }
  )JSON",
                          render, bid, egress_payload_string,
                          debug_reporting_urls,
                          temporary_egress_payload_string);
}

void SetupContextualProtectedAppSignalsRomaExpectations(
    MockV8DispatchClient& dispatcher, int& num_roma_dispatches,
    absl::optional<std::string> generate_bid_udf_response) {
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&num_roma_dispatches, &generate_bid_udf_response](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback batch_callback) {
        ++num_roma_dispatches;
        return MockRomaExecution(batch, std::move(batch_callback),
                                 kGenerateBidEntryFunction,
                                 kProtectedAppSignalsGenerateBidBlobVersion,
                                 generate_bid_udf_response.has_value()
                                     ? *generate_bid_udf_response
                                     : CreateGenerateBidsUdfResponse());
      });
}

void SetupProtectedAppSignalsRomaExpectations(
    MockV8DispatchClient& dispatcher, int& num_roma_dispatches,
    const absl::optional<std::string>&
        prepare_data_for_ad_retrieval_udf_response,
    const absl::optional<std::string>& generate_bid_udf_response) {
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&num_roma_dispatches,
                       prepare_data_for_ad_retrieval_udf_response,
                       generate_bid_udf_response](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback batch_callback) {
        ++num_roma_dispatches;
        PS_VLOG(5) << "generate_bid_udf_response has value: "
                   << generate_bid_udf_response.has_value();
        if (num_roma_dispatches == 1) {
          // First dispatch happens for `prepareDataForAdRetrieval` UDF.
          return MockRomaExecution(
              batch, std::move(batch_callback),
              kPrepareDataForAdRetrievalEntryFunctionName,
              kPrepareDataForAdRetrievalBlobVersion,
              prepare_data_for_ad_retrieval_udf_response.has_value()
                  ? *prepare_data_for_ad_retrieval_udf_response
                  : CreatePrepareDataForAdsRetrievalResponse());
        } else {
          // Second dispatch happens for `generateBid` UDF.
          return MockRomaExecution(batch, std::move(batch_callback),
                                   kGenerateBidEntryFunction,
                                   kProtectedAppSignalsGenerateBidBlobVersion,
                                   generate_bid_udf_response.has_value()
                                       ? *generate_bid_udf_response
                                       : CreateGenerateBidsUdfResponse());
        }
      });
}

absl::StatusOr<kv_server::v2::GetValuesResponse>
CreateAdsRetrievalOrKvLookupResponse(absl::string_view ads) {
  kv_server::v2::GetValuesResponse response;
  PS_RETURN_IF_ERROR(JsonStringToMessage(absl::Substitute(R"JSON(
                                            {
                                              "singlePartition": {
                                                "id": 0,
                                                "stringOutput": "$0"
                                              }
                                            })JSON",
                                                          ads),
                                         &response));
  return response;
}

void SetupAdRetrievalClientExpectations(
    KVAsyncClientMock& ad_retrieval_client,
    absl::optional<absl::StatusOr<GetValuesResponse>> ads_retrieval_response) {
  EXPECT_CALL(ad_retrieval_client, ExecuteInternal)
      .WillOnce([&ads_retrieval_response](
                    std::unique_ptr<GetValuesRequest> raw_request,
                    grpc::ClientContext* context,
                    absl::AnyInvocable<void(
                        absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                        ResponseMetadata)&&>
                        on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        auto response = ads_retrieval_response.has_value()
                            ? *ads_retrieval_response
                            : CreateAdsRetrievalOrKvLookupResponse();
        EXPECT_TRUE(response.ok()) << response.status();
        std::move(on_done)(
            std::make_unique<GetValuesResponse>(*std::move(response)),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });
}

}  // namespace privacy_sandbox::bidding_auction_servers
