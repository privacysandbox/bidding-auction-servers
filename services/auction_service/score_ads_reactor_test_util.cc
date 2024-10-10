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

#include "services/auction_service/score_ads_reactor_test_util.h"

#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
template <typename Request>
void SetupMockCryptoClientWrapper(Request request,
                                  MockCryptoClientWrapper& crypto_client) {
  // Mock the HpkeEncrypt() call on the crypto client.
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
      hpke_encrypt_response;
  hpke_encrypt_response.set_secret(kSecret);
  hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kKeyId);
  hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
      request.SerializeAsString());
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
      .WillOnce(testing::Return(hpke_encrypt_response));

  // Mock the HpkeDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
      hpke_decrypt_response;
  hpke_decrypt_response.set_payload(request.SerializeAsString());
  hpke_decrypt_response.set_secret(kSecret);
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(hpke_decrypt_response));

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.

  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(testing::AnyNumber())
      .WillOnce(
          [](absl::string_view plaintext_payload, absl::string_view secret) {
            google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
            data.set_ciphertext(plaintext_payload);
            google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
                aead_encrypt_response;
            *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
            return aead_encrypt_response;
          });

  // Mock the AeadDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse
      aead_decrypt_response;
  aead_decrypt_response.set_payload(request.SerializeAsString());
  EXPECT_CALL(crypto_client, AeadDecrypt)
      .Times(testing::AnyNumber())
      .WillOnce(testing::Return(aead_decrypt_response));
}
}  // namespace

ProtectedAppSignalsAdWithBidMetadata GetProtectedAppSignalsAdWithBidMetadata(
    absl::string_view render_url, float bid) {
  ProtectedAppSignalsAdWithBidMetadata ad;
  ad.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(render_url, "arbitraryMetadataKey", 2));
  ad.set_render(render_url);
  ad.set_bid(bid);
  ad.set_owner(kTestProtectedAppSignalsAdOwner);
  return ad;
}

void SetupTelemetryCheck(const ScoreAdsRequest& request) {
  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
  metric::MetricContextMap<ScoreAdsRequest>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto))
      ->Get(&request);
}

ScoreAdsReactorTestHelper::ScoreAdsReactorTestHelper() {
  SetupTelemetryCheck(request_);
  config_client_.SetOverride(kTrue, TEST_MODE);

  key_fetcher_manager_ =
      CreateKeyFetcherManager(config_client_, /*public_key_fetcher=*/nullptr);
}

ScoreAdsResponse ScoreAdsReactorTestHelper::ExecuteScoreAds(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
    MockV8DispatchClient& dispatcher,
    const AuctionServiceRuntimeConfig& runtime_config) {
  SetupMockCryptoClientWrapper(raw_request, crypto_client_);
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  request_.set_key_id(kKeyId);

  ScoreAdsResponse response;
  grpc::CallbackServerContext context;
  ScoreAdsReactor reactor(&context, dispatcher, &request_, &response,
                          std::move(benchmarkingLogger_),
                          key_fetcher_manager_.get(), &crypto_client_,
                          async_reporter.get(), runtime_config);
  reactor.Execute();
  return response;
}

absl::Status FakeExecute(std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback,
                         std::vector<std::string> json_ad_scores,
                         const bool call_wrapper_method,
                         const bool enable_adtech_code_logging) {
  std::vector<absl::StatusOr<DispatchResponse>> responses;
  auto json_ad_score_itr = json_ad_scores.begin();

  for (const auto& request : batch) {
    if (std::strcmp(request.handler_name.c_str(),
                    kReportingDispatchHandlerFunctionName) != 0 &&
        std::strcmp(request.handler_name.c_str(), kReportResultEntryFunction) !=
            0 &&
        std::strcmp(request.handler_name.c_str(), kReportWinEntryFunction) !=
            0) {
      EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
    }
    DispatchResponse dispatch_response = {};
    dispatch_response.resp = *json_ad_score_itr++;
    dispatch_response.id = request.id;
    responses.emplace_back(dispatch_response);
  }
  done_callback(responses);
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
