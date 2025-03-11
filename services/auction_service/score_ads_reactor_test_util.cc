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
  ad.set_bid_currency(kUsdIsoCode);
  ad.set_owner(kTestProtectedAppSignalsAdOwner);
  ad.set_egress_payload(kTestEgressPayload);
  ad.set_temporary_unlimited_egress_payload(kTestTemporaryEgressPayload);
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

ScoreAdsRequest::ScoreAdsRawRequest BuildRawRequest(
    std::vector<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>
        ads_with_bids_to_add,
    const ScoreAdsRawRequestOptions& options) {
  ScoreAdsRequest::ScoreAdsRawRequest output;
  for (int i = 0; i < ads_with_bids_to_add.size(); i++) {
    output.mutable_per_buyer_signals()->try_emplace(
        ads_with_bids_to_add[i].interest_group_owner(), kTestBuyerSignalsObj);
    *output.add_ad_bids() = std::move(ads_with_bids_to_add[i]);
  }
  output.set_seller_signals(options.seller_signals);
  output.set_auction_signals(options.auction_signals);
  output.set_scoring_signals(options.scoring_signals);
  output.set_publisher_hostname(options.publisher_hostname);
  output.set_enable_debug_reporting(options.enable_debug_reporting);
  output.set_seller_currency(options.seller_currency);
  output.set_top_level_seller(options.top_level_seller);
  output.set_seller(kTestSeller);
  output.mutable_consented_debug_config()->set_is_consented(
      options.enable_adtech_code_logging);
  output.mutable_consented_debug_config()->set_token(kTestConsentToken);
  output.set_seller_data_version(options.seller_data_version);
  return output;
}

ScoreAdsRequest::ScoreAdsRawRequest BuildProtectedAppSignalsRawRequest(
    std::vector<ScoreAdsRequest::ScoreAdsRawRequest::
                    ProtectedAppSignalsAdWithBidMetadata>
        ads_with_bids_to_add,
    const ScoreAdsRawRequestOptions& options) {
  ScoreAdsRequest::ScoreAdsRawRequest output;
  for (int i = 0; i < ads_with_bids_to_add.size(); i++) {
    output.mutable_per_buyer_signals()->try_emplace(
        ads_with_bids_to_add[i].owner(), kTestBuyerSignalsObj);
    *output.add_protected_app_signals_ad_bids() =
        std::move(ads_with_bids_to_add[i]);
  }
  output.set_seller_signals(options.seller_signals);
  output.set_auction_signals(options.auction_signals);
  output.set_scoring_signals(options.scoring_signals);
  output.set_publisher_hostname(options.publisher_hostname);
  output.set_enable_debug_reporting(options.enable_debug_reporting);
  output.set_seller_currency(options.seller_currency);
  output.set_top_level_seller(options.top_level_seller);
  output.set_seller(kTestSeller);
  output.mutable_consented_debug_config()->set_is_consented(
      options.enable_adtech_code_logging);
  output.mutable_consented_debug_config()->set_token(kTestConsentToken);
  return output;
}

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
BuildTestAdWithBidMetadata(const AdWithBidMetadataParams& params) {
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad_with_bid_metadata;
  if (params.make_metadata) {
    ad_with_bid_metadata.mutable_ad()->mutable_struct_value()->MergeFrom(
        MakeAnAd(params.render_url, params.metadata_key,
                 params.metadata_value));
  }

  // This must have an entry in kTestScoringSignals.
  ad_with_bid_metadata.set_render(params.render_url);
  ad_with_bid_metadata.set_bid(params.bid);
  ad_with_bid_metadata.set_bid_currency(params.bid_currency);
  ad_with_bid_metadata.set_interest_group_name(params.interest_group_name);
  ad_with_bid_metadata.set_interest_group_owner(params.interest_group_owner);
  ad_with_bid_metadata.set_interest_group_origin(params.interest_group_origin);
  for (int i = 0; i < params.number_of_component_ads; i++) {
    ad_with_bid_metadata.add_ad_components(
        absl::StrCat(params.ad_component_render_url_base, i));
  }
  if (params.buyer_reporting_id && !params.buyer_reporting_id->empty()) {
    ad_with_bid_metadata.set_buyer_reporting_id(*params.buyer_reporting_id);
  }
  if (params.buyer_and_seller_reporting_id &&
      !params.buyer_and_seller_reporting_id->empty()) {
    ad_with_bid_metadata.set_buyer_and_seller_reporting_id(
        *params.buyer_and_seller_reporting_id);
  }
  if (params.selected_buyer_and_seller_reporting_id &&
      !params.selected_buyer_and_seller_reporting_id->empty()) {
    ad_with_bid_metadata.set_selected_buyer_and_seller_reporting_id(
        *params.selected_buyer_and_seller_reporting_id);
  }
  ad_with_bid_metadata.set_ad_cost(params.ad_cost);
  ad_with_bid_metadata.set_data_version(params.data_version);
  ad_with_bid_metadata.set_k_anon_status(params.k_anon_status);
  PS_VLOG(5) << "Generated ad bid with metadata: " << ad_with_bid_metadata;
  return ad_with_bid_metadata;
}

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
GetTestAdWithBidBarbecueWithComponents() {
  std::string render_url = "barStandardAds.com/render_ad?id=barbecue2";
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad_with_bid =
      BuildTestAdWithBidMetadata(
          {// This must have an entry in kTestScoringSignals.
           .render_url = render_url,
           .bid = 17.76,
           .interest_group_name = kBarbecureIgName,
           .interest_group_owner = kInterestGroupOwnerOfBarBidder,
           .number_of_component_ads = 1,
           .ad_component_render_url_base =
               "barStandardAds.com/ad_components/id=",
           .make_metadata = false});
  auto* bar_ad_map =
      ad_with_bid.mutable_ad()->mutable_struct_value()->mutable_fields();
  bar_ad_map->try_emplace(kAdMetadataPropertyNameRenderUrl,
                          MakeAStringValue(render_url));
  bar_ad_map->try_emplace(kAdMetadataPropertyNameMetadata,
                          MakeAListValue({
                              MakeAStringValue("brisket"),
                              MakeAStringValue("pulled_pork"),
                              MakeAStringValue("smoked_chicken"),
                          }));
  return ad_with_bid;
}

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata GetTestAdWithBidBar(
    absl::string_view buyer_reporting_id) {
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata bar =
      BuildTestAdWithBidMetadata(
          {// This must have an entry in kTestScoringSignals.
           .render_url = kBarRenderUrl,
           .bid = 2,
           .interest_group_name = "ig_bar",
           .interest_group_owner = kInterestGroupOwnerOfBarBidder,
           .buyer_reporting_id = buyer_reporting_id,
           .ad_component_render_url_base = "adComponent.com/bar_components/id=",
           .bid_currency = kUsdIsoCode,
           .make_metadata = false});

  auto* bar_ad_map = bar.mutable_ad()->mutable_struct_value()->mutable_fields();
  bar_ad_map->try_emplace(kAdMetadataPropertyNameRenderUrl,
                          MakeAStringValue(kBarRenderUrl));
  bar_ad_map->try_emplace(kAdMetadataPropertyNameMetadata,
                          MakeAListValue({
                              MakeAStringValue("140583167746"),
                              MakeAStringValue("627640802621"),
                              MakeANullValue(),
                              MakeAStringValue("18281019067"),
                          }));
  return bar;
}

void PopulateTestAdWithBidMetdata(
    const PostAuctionSignals& post_auction_signals,
    const BuyerReportingDispatchRequestData& buyer_dispatch_data,
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata&
        ad_with_bid_metadata) {
  int number_of_component_ads = 3;
  ad_with_bid_metadata.mutable_ad()->mutable_struct_value()->MergeFrom(MakeAnAd(
      post_auction_signals.winning_ad_render_url, "arbitraryMetadataKey", 2));

  // This must have an entry in kTestScoringSignals.
  ad_with_bid_metadata.set_render(post_auction_signals.winning_ad_render_url);
  ad_with_bid_metadata.set_bid(post_auction_signals.winning_bid);
  ad_with_bid_metadata.set_interest_group_name(
      buyer_dispatch_data.interest_group_name);
  ad_with_bid_metadata.set_interest_group_owner(
      post_auction_signals.winning_ig_owner);
  ad_with_bid_metadata.set_interest_group_origin(kInterestGroupOrigin);
  for (int i = 0; i < number_of_component_ads; i++) {
    ad_with_bid_metadata.add_ad_components(
        absl::StrCat("adComponent.com/foo_components/id=", i));
  }
  if (buyer_dispatch_data.buyer_reporting_id) {
    ad_with_bid_metadata.set_buyer_reporting_id(
        *buyer_dispatch_data.buyer_reporting_id);
  }
  if (buyer_dispatch_data.buyer_and_seller_reporting_id) {
    ad_with_bid_metadata.set_buyer_and_seller_reporting_id(
        *buyer_dispatch_data.buyer_and_seller_reporting_id);
  }
  if (buyer_dispatch_data.selected_buyer_and_seller_reporting_id) {
    ad_with_bid_metadata.set_selected_buyer_and_seller_reporting_id(
        *buyer_dispatch_data.selected_buyer_and_seller_reporting_id);
  }
  ad_with_bid_metadata.set_interest_group_name(
      buyer_dispatch_data.interest_group_name);
  ad_with_bid_metadata.set_ad_cost(kTestAdCost);
  ad_with_bid_metadata.set_bid(post_auction_signals.winning_bid);
  ad_with_bid_metadata.set_bid_currency(kEurosIsoCode);
  ad_with_bid_metadata.set_data_version(buyer_dispatch_data.data_version);
}

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
GetTestAdWithBidBarbecue() {
  const std::string render_url = "barStandardAds.com/render_ad?id=barbecue";
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad_with_bid =
      BuildTestAdWithBidMetadata(
          {// This must have an entry in kTestScoringSignals.
           .render_url = render_url,
           .bid = 17.76,
           .interest_group_name = kBarbecureIgName,
           .interest_group_owner = kInterestGroupOwnerOfBarBidder,
           .number_of_component_ads = 0,
           .make_metadata = false});

  auto* bar_ad_map =
      ad_with_bid.mutable_ad()->mutable_struct_value()->mutable_fields();
  bar_ad_map->try_emplace(kAdMetadataPropertyNameRenderUrl,
                          MakeAStringValue(render_url));
  bar_ad_map->try_emplace(kAdMetadataPropertyNameMetadata,
                          MakeAListValue({
                              MakeAStringValue("brisket"),
                              MakeAStringValue("pulled_pork"),
                              MakeAStringValue("smoked_chicken"),
                          }));
  return ad_with_bid;
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
                          *async_reporter, runtime_config);
  reactor.Execute();
  return response;
}

absl::Status FakeExecute(
    std::vector<DispatchRequest>& batch,
    BatchDispatchDoneCallback done_callback,
    absl::flat_hash_map</*id=*/std::string, /*json_score*/ std::string>
        json_ad_scores,
    const bool call_wrapper_method, const bool enable_adtech_code_logging) {
  std::vector<absl::StatusOr<DispatchResponse>> responses;

  for (const auto& request : batch) {
    if (std::strcmp(request.handler_name.c_str(),
                    kReportingDispatchHandlerFunctionName) != 0 &&
        std::strcmp(request.handler_name.c_str(), kReportResultEntryFunction) !=
            0 &&
        std::strcmp(request.handler_name.c_str(), kReportWinEntryFunction) !=
            0) {
      EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
    }
    DispatchResponse dispatch_response;
    auto it = json_ad_scores.find(request.id);
    CHECK(it != json_ad_scores.end());
    dispatch_response.resp = it->second;
    dispatch_response.id = request.id;
    PS_VLOG(5) << ">>>> Associated id: " << dispatch_response.id
               << ", with response: " << dispatch_response.resp;
    responses.emplace_back(std::move(dispatch_response));
  }
  done_callback(responses);
  return absl::OkStatus();
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
    DispatchResponse dispatch_response;
    dispatch_response.resp = *json_ad_score_itr++;
    dispatch_response.id = request.id;
    responses.emplace_back(std::move(dispatch_response));
  }
  done_callback(responses);
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
