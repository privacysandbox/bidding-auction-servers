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

#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"

#include <gmock/gmock-matchers.h>

#include <memory>
#include <string>
#include <utility>

#include <google/protobuf/util/message_differencer.h>
#include <include/gmock/gmock-actions.h>
#include <include/gmock/gmock-nice-strict.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "services/common/private_aggregation/private_aggregation_test_util.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/util/oblivious_http_utils.h"
#include "services/seller_frontend_service/select_ad_reactor.h"
#include "services/seller_frontend_service/util/framing_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::testing::Matcher;
using ::testing::Return;
using GetBidDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>,
         ResponseMetadata) &&>;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ScoringSignalsDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<ScoringSignals>>,
                            GetByteSize) &&>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = ::google::protobuf::Map<std::string, BuyerInput>;
using ::testing::An;
using ::testing::AnyNumber;
using ::testing::Cardinality;

absl::flat_hash_map<std::string, std::string> BuildBuyerWinningAdUrlMap(
    const SelectAdRequest& request) {
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url;
  for (const std::string& buyer_ig_owner :
       request.auction_config().buyer_list()) {
    buyer_to_ad_url.insert_or_assign(buyer_ig_owner,
                                     absl::StrCat(buyer_ig_owner, "/ad"));
  }
  return buyer_to_ad_url;
}

void SetupMockCryptoClient(MockCryptoClientWrapper& crypto_client) {
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .WillOnce([](const PublicKey& key, const std::string& plaintext_payload) {
        // Mock the HpkeEncrypt() call on the crypto client.
        google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
            hpke_encrypt_response;
        hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
            plaintext_payload);
        return hpke_encrypt_response;
      });
}

Cardinality BuyerCallCardinality(bool expect_all_buyers_solicited,
                                 bool repeated_get_allowed) {
  if (expect_all_buyers_solicited && repeated_get_allowed) {
    return testing::AtLeast(1);
  } else if (expect_all_buyers_solicited && !repeated_get_allowed) {
    return testing::Exactly(1);
  } else if (!expect_all_buyers_solicited && repeated_get_allowed) {
    return AnyNumber();
  } else {
    return testing::AtMost(1);
  }
}

GetBidsResponse::GetBidsRawResponse BuildGetBidsResponseWithSingleAd(
    absl::string_view ad_url, const BuildNewAdWithBidOptions& options) {
  AdWithBid bid = BuildNewAdWithBid(ad_url, options);
  GetBidsResponse::GetBidsRawResponse response;
  response.mutable_bids()->Add(std::move(bid));
  return response;
}

void SetupBuyerClientMock(
    absl::string_view hostname,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients,
    const std::optional<GetBidsResponse::GetBidsRawResponse>& bid,
    const SetupBuyerClientMockOptions& options) {
  auto MockGetBids =
      [bid, options](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
          grpc::ClientContext* context, GetBidDoneCallback on_done,
          absl::Duration timeout, RequestConfig request_config) {
        ABSL_LOG(INFO) << "Returning mock bids";
        // Check top level seller is populated for component auctions.
        if (!options.top_level_seller.empty()) {
          EXPECT_EQ(get_values_request->top_level_seller(),
                    options.top_level_seller);
        }
        if (bid) {
          std::move(on_done)(
              std::make_unique<GetBidsResponse::GetBidsRawResponse>(*bid),
              /* response_metadata= */ {});
        }
        if (options.num_buyers_solicited) {
          ++(*options.num_buyers_solicited);
        }
        return absl::OkStatus();
      };
  auto SetupMockBuyer =
      [MockGetBids,
       options](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal)
            .Times(BuyerCallCardinality(options.expect_all_buyers_solicited,
                                        options.repeated_get_allowed))
            .WillRepeatedly(MockGetBids);
        return buyer;
      };

  auto MockBuyerFactoryCall = [SetupMockBuyer](absl::string_view hostname) {
    return SetupMockBuyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_clients, Get(hostname))
      .Times(BuyerCallCardinality(options.expect_all_buyers_solicited,
                                  options.repeated_get_allowed))
      .WillRepeatedly(MockBuyerFactoryCall);
}

AdWithBid BuildAdWithBidFromAdWithBidMetadata(
    const AdWithBidMetadata& input, absl::string_view buyer_reporting_id,
    absl::string_view buyer_and_seller_reporting_id,
    absl::string_view selected_buyer_and_seller_reporting_id) {
  AdWithBid result;
  if (input.has_ad()) {
    *result.mutable_ad() = input.ad();
  }
  result.set_bid(input.bid());
  result.set_render(input.render());
  result.set_bid_currency(input.bid_currency());
  result.mutable_ad_components()->CopyFrom(input.ad_components());
  result.set_allow_component_auction(input.allow_component_auction());
  result.set_interest_group_name(input.interest_group_name());
  result.set_ad_cost(kAdCost);
  result.set_modeling_signals(kModelingSignals);
  result.set_data_version(input.data_version());
  if (!buyer_reporting_id.empty()) {
    result.set_buyer_reporting_id(buyer_reporting_id);
  }
  if (!buyer_and_seller_reporting_id.empty()) {
    result.set_buyer_and_seller_reporting_id(buyer_and_seller_reporting_id);
  }
  if (!selected_buyer_and_seller_reporting_id.empty()) {
    result.set_selected_buyer_and_seller_reporting_id(
        selected_buyer_and_seller_reporting_id);
  }
  return result;
}

AdWithBid BuildNewAdWithBid(absl::string_view ad_url,
                            const BuildNewAdWithBidOptions& options) {
  AdWithBid bid;
  bid.set_render(ad_url);
  for (int i = 0; i < options.number_ad_component_render_urls; i++) {
    bid.add_ad_components(
        absl::StrCat("https://fooAds.com/adComponents?id=", i));
  }
  bid.set_bid(options.bid_value);
  bid.set_interest_group_name(options.interest_group_name);
  bid.set_bid_currency(options.bid_currency);
  bid.set_ad_cost(kAdCost);
  bid.set_modeling_signals(kModelingSignals);

  if (options.enable_event_level_debug_reporting) {
    DebugReportUrls debug_report_urls;
    debug_report_urls.set_auction_debug_win_url(
        absl::StrCat(kTestBuyerDebugWinUrlPrefix, ad_url));
    debug_report_urls.set_auction_debug_loss_url(
        absl::StrCat(kTestBuyerDebugLossUrlPrefix, ad_url));
    *bid.mutable_debug_report_urls() = debug_report_urls;
  }
  if (!options.buyer_reporting_id.empty()) {
    bid.set_buyer_reporting_id(options.buyer_reporting_id);
  }
  PrivateAggregateContribution win_object_contribution =
      GetTestContributionWithSignalObjects(EVENT_TYPE_WIN, "");
  PrivateAggregateContribution loss_object_contribution =
      GetTestContributionWithSignalObjects(EVENT_TYPE_LOSS, "");
  PrivateAggregateContribution win_int_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  PrivateAggregateContribution loss_int_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_LOSS, "");
  for (const auto& contribution : options.contributions) {
    *bid.add_private_aggregation_contributions() = contribution;
  }
  // Optional proto field hence check.
  if (!options.buyer_and_seller_reporting_id.empty()) {
    bid.set_buyer_and_seller_reporting_id(
        options.buyer_and_seller_reporting_id);
  }
  // Optional proto field hence check.
  if (!options.selected_buyer_and_seller_reporting_id.empty()) {
    bid.set_selected_buyer_and_seller_reporting_id(
        options.selected_buyer_and_seller_reporting_id);
  }
  bid.set_data_version(options.data_version);
  return bid;
}

ProtectedAppSignalsAdWithBid BuildNewPASAdWithBid(
    const std::string& ad_render_url, absl::optional<float> bid_value,
    const bool enable_event_level_debug_reporting,
    absl::optional<absl::string_view> bid_currency) {
  ProtectedAppSignalsAdWithBid pas_ad_with_bid;
  pas_ad_with_bid.set_render(ad_render_url);
  if (bid_value) {
    pas_ad_with_bid.set_bid(bid_value.value());
  }
  if (bid_currency) {
    pas_ad_with_bid.set_bid_currency(bid_currency.value());
  }
  pas_ad_with_bid.set_ad_cost(kAdCost);

  if (enable_event_level_debug_reporting) {
    DebugReportUrls debug_report_urls;
    debug_report_urls.set_auction_debug_win_url(
        "https://pas_test.com/debugWin?render=" + ad_render_url);
    debug_report_urls.set_auction_debug_loss_url(
        "https://pas_test.com/debugLoss?render=" + ad_render_url);
    *pas_ad_with_bid.mutable_debug_report_urls() = debug_report_urls;
  }
  return pas_ad_with_bid;
}

server_common::PrivateKey GetPrivateKey() {
  HpkeKeyset default_keyset = HpkeKeyset{};
  server_common::PrivateKey private_key;
  private_key.key_id = std::to_string(default_keyset.key_id);
  private_key.private_key = GetHpkePrivateKey(default_keyset.private_key);
  return private_key;
}

void SetupScoringProviderMock(
    const MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>& provider,
    const BuyerBidsResponseMap& expected_buyer_bids,
    const ScoringProviderMockOptions& options) {
  // options is passed as a copy and not a reference as otherwise we would need
  // to make sure options always outlives when the lambda is called, which would
  // cost a hundred lines of boilerplate and be prone to mistakes.
  auto MockScoringSignalsProvider =
      [&expected_buyer_bids, options](
          const ScoringSignalsRequest& scoring_signals_request,
          ScoringSignalsDoneCallback on_done, absl::Duration timeout,
          RequestContext context) {
        if (options.expected_num_bids > -1) {
          EXPECT_EQ(scoring_signals_request.buyer_bids_map_.size(),
                    options.expected_num_bids);
        } else {
          EXPECT_EQ(scoring_signals_request.buyer_bids_map_.size(),
                    expected_buyer_bids.size());
        }
        google::protobuf::util::MessageDifferencer diff;
        std::string diff_output;
        diff.ReportDifferencesToString(&diff_output);

        // Finds one expected bid for every actual bid.
        // When sets are equal size this is a full equality check.
        // When there are more 'expected' than actual this will not error.
        for (const auto& [unused, actual_get_bids_raw_response] :
             scoring_signals_request.buyer_bids_map_) {
          EXPECT_TRUE(std::any_of(
              expected_buyer_bids.begin(), expected_buyer_bids.end(),
              [&diff,
               actual = actual_get_bids_raw_response.get()](auto& expected) {
                return diff.Compare(*actual, *expected.second);
              }))
              << diff_output;
        }

        EXPECT_EQ(scoring_signals_request.seller_kv_experiment_group_id_,
                  options.seller_egid);

        GetByteSize get_byte_size;
        if (options.server_status_to_return.ok()) {
          auto scoring_signals = std::make_unique<ScoringSignals>();
          scoring_signals->scoring_signals =
              std::make_unique<std::string>(options.scoring_signals_value);
          scoring_signals->data_version = options.data_version;
          std::move(on_done)(std::move(scoring_signals), get_byte_size);
        } else {
          std::move(on_done)(options.server_status_to_return, get_byte_size);
        }
      };
  if (options.repeated_get_allowed) {
    EXPECT_CALL(provider, Get).WillRepeatedly(MockScoringSignalsProvider);
  } else {
    EXPECT_CALL(provider, Get).WillOnce(MockScoringSignalsProvider);
  }
}

void SetupKvAsyncClientMock(KVAsyncClientMock& kv_async_client,
                            const kv_server::v2::GetValuesResponse& response,
                            const BuyerBidsResponseMap& expected_buyer_bids,
                            const KvAsyncMockOptions& options) {
  auto kv_mock_response =
      [&expected_buyer_bids, response, options](
          std::unique_ptr<GetValuesRequest> get_values_raw_request,
          grpc::ClientContext* context, auto on_done, absl::Duration timeout,
          RequestConfig request_config) {
        if (options.expected_num_ads > -1) {
          EXPECT_EQ(get_values_raw_request->partitions().size(),
                    options.expected_num_ads);
        } else {
          int num_ads = 0;
          for (const auto& [unused_buyer, get_bids_response] :
               expected_buyer_bids) {
            num_ads += get_bids_response->bids().size();
            num_ads += get_bids_response->protected_app_signals_bids().size();
          }
          EXPECT_EQ(get_values_raw_request->partitions().size(), num_ads);
        }

        EXPECT_EQ(get_values_raw_request->metadata()
                      .fields()
                      .at(kKvExperimentGroupId)
                      .string_value(),
                  options.seller_egid);

        if (options.server_status_to_return.ok()) {
          std::move(on_done)(std::make_unique<GetValuesResponse>(response),
                             /* response_metadata= */ {});
        } else {
          std::move(on_done)(options.server_status_to_return,
                             /* response_metadata= */ {});
        }
        return options.server_status_to_return;
      };
  if (options.repeated_get_allowed) {
    EXPECT_CALL(kv_async_client, ExecuteInternal)
        .WillRepeatedly(kv_mock_response);
  } else {
    EXPECT_CALL(kv_async_client, ExecuteInternal).WillOnce(kv_mock_response);
  }
}

TrustedServersConfigClient CreateConfig() {
  TrustedServersConfigClient config({});
  config.SetOverride("1", GET_BID_RPC_TIMEOUT_MS);
  config.SetOverride("2", KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS);
  config.SetOverride("3", SCORE_ADS_RPC_TIMEOUT_MS);
  config.SetOverride(kAuctionHost, AUCTION_SERVER_HOST);
  config.SetOverride(
      "auction-seller1-tjs-appmesh-virtual-server.seller1-frontend.com",
      GRPC_ARG_DEFAULT_AUTHORITY_VAL);
  config.SetOverride(kTrue, ENABLE_SELLER_FRONTEND_BENCHMARKING);
  config.SetOverride(kSellerOriginDomain, SELLER_ORIGIN_DOMAIN);
  config.SetOverride(kTrue, ENABLE_PROTECTED_AUDIENCE);
  return config;
}

// Gets the encoded and encrypted request as well as the OHTTP context used
// for encrypting the request.
std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetFramedInputAndOhttpContext(absl::string_view encoded_request) {
  absl::StatusOr<std::string> framed_request =
      server_common::EncodeResponsePayload(
          server_common::CompressionType::kGzip, encoded_request,
          GetEncodedDataSize(encoded_request.size()));
  EXPECT_TRUE(framed_request.ok()) << framed_request.status().message();
  HpkeKeyset keyset;
  auto ohttp_request = CreateValidEncryptedRequest(*framed_request, keyset);
  EXPECT_TRUE(ohttp_request.ok()) << ohttp_request.status().message();
  std::string encrypted_request =
      '\0' + ohttp_request->EncapsulateAndSerialize();
  auto context = std::move(*ohttp_request).ReleaseContext();
  return {std::move(encrypted_request), std::move(context)};
}

AuctionResult DecryptAppProtoAuctionResult(
    std::string& auction_result_ciphertext,
    quiche::ObliviousHttpRequest::Context& context) {
  AuctionResult default_empty_auction_result;
  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      auction_result_ciphertext, context, kBiddingAuctionOhttpResponseLabel);
  EXPECT_TRUE(decrypted_response.ok()) << decrypted_response.status();
  // Can't assert due to return type.
  if (!decrypted_response.ok()) {
    return default_empty_auction_result;
  }

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();
  // Can't assert due to return type.
  if (!decompressed_response.ok()) {
    return default_empty_auction_result;
  }

  // Validate the error message returned in the response.
  AuctionResult deserialized_auction_result;
  // Can't assert due to return type.
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));

  return deserialized_auction_result;
}

std::pair<AuctionResult, std::string> DecryptBrowserAuctionResultAndNonce(
    std::string& auction_result_ciphertext,
    quiche::ObliviousHttpRequest::Context& context) {
  std::pair<AuctionResult, std::string> result;
  result.second = "";
  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      auction_result_ciphertext, context, kBiddingAuctionOhttpResponseLabel);
  EXPECT_TRUE(decrypted_response.ok()) << decrypted_response.status();
  // Can't assert due to return type.
  if (!decrypted_response.ok()) {
    return result;
  }

  // Decompress the encoded response.
  auto decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  EXPECT_TRUE(decompressed_response.ok()) << decompressed_response.status();
  // Can't assert due to return type.
  if (!decompressed_response.ok()) {
    return result;
  }

  absl::StatusOr<std::pair<AuctionResult, std::string>>
      deserialized_auction_result =
          CborDecodeAuctionResultAndNonceToProto(*decompressed_response);
  EXPECT_TRUE(deserialized_auction_result.ok())
      << deserialized_auction_result.status();
  // Can't assert due to return type.
  if (!deserialized_auction_result.ok()) {
    return result;
  }
  return *deserialized_auction_result;
}

absl::StatusOr<std::string> UnframeAndDecompressAuctionResult(
    absl::string_view framed_response) {
  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(framed_response);
  EXPECT_TRUE(unframed_response.ok()) << unframed_response.status().message();

  // Decompress the encoded response.
  return GzipDecompress(unframed_response->compressed_data);
}

std::string FrameAndCompressProto(absl::string_view serialized_proto) {
  // Compress the bytes array before framing it with pre-amble and padding.
  absl::StatusOr<std::string> compressed_data = GzipCompress(serialized_proto);
  EXPECT_TRUE(compressed_data.ok()) << compressed_data.status().message();
  absl::StatusOr<std::string> framed_request =
      server_common::EncodeResponsePayload(
          server_common::CompressionType::kGzip, *compressed_data,
          GetEncodedDataSize(compressed_data->size()));
  EXPECT_TRUE(framed_request.ok()) << framed_request.status().message();
  return *framed_request;
}

void FillInAdRenderUrlAndCurrency(
    absl::string_view matching_currency, absl::string_view mismatching_currency,
    absl::string_view base_ad_render_url, const int current_iteration,
    int& mismatched_left_to_add, int& matched_left_to_add,
    std::string& ad_render_url, std::string& bid_currency) {
  if ((mismatched_left_to_add > 0) && (matched_left_to_add > 0)) {
    if ((current_iteration % 2) == 0) {
      ad_render_url = absl::StrCat(base_ad_render_url, "/", current_iteration,
                                   "_", matching_currency);
      bid_currency = matching_currency;
      matched_left_to_add--;
    } else {
      ad_render_url = absl::StrCat(base_ad_render_url, "/", current_iteration,
                                   "_", mismatching_currency);
      bid_currency = mismatching_currency;
      mismatched_left_to_add--;
    }
  } else if (matched_left_to_add > 0) {
    ad_render_url = absl::StrCat(base_ad_render_url, "/", current_iteration,
                                 "_", matching_currency);
    bid_currency = matching_currency;
    matched_left_to_add--;
  } else {
    ad_render_url = absl::StrCat(base_ad_render_url, "/", current_iteration,
                                 "_", mismatching_currency);
    bid_currency = mismatching_currency;
    mismatched_left_to_add--;
  }
}

std::vector<AdWithBid> GetAdWithBidsInMultipleCurrencies(
    const int num_ad_with_bids, const int num_mismatched,
    absl::string_view matching_currency, absl::string_view mismatching_currency,
    absl::string_view base_ad_render_url, absl::string_view base_ig_name) {
  DCHECK(num_ad_with_bids >= num_mismatched);

  int mismatched_left_to_add = num_mismatched;
  int matched_left_to_add = num_ad_with_bids - num_mismatched;

  std::vector<AdWithBid> ads_with_bids;
  ads_with_bids.reserve(num_ad_with_bids);
  for (int i = 0; i < num_ad_with_bids; i++) {
    std::string ad_render_url;
    std::string bid_currency;

    FillInAdRenderUrlAndCurrency(matching_currency, mismatching_currency,
                                 base_ad_render_url, i, mismatched_left_to_add,
                                 matched_left_to_add, ad_render_url,
                                 bid_currency);
    ads_with_bids.push_back(BuildNewAdWithBid(
        ad_render_url,
        {.interest_group_name = absl::StrCat(base_ig_name, "_", i),
         .bid_value = static_cast<float>(1 + (0.001 * i)),
         .bid_currency = bid_currency}));
  }
  DCHECK_EQ(matched_left_to_add, 0);
  DCHECK_EQ(mismatched_left_to_add, 0);
  return ads_with_bids;
}

std::vector<ProtectedAppSignalsAdWithBid> GetPASAdWithBidsInMultipleCurrencies(
    const int num_ad_with_bids, const int num_mismatched,
    absl::string_view matching_currency, absl::string_view mismatching_currency,
    absl::string_view base_ad_render_url) {
  DCHECK(num_ad_with_bids >= num_mismatched);

  int mismatched_left_to_add = num_mismatched;
  int matched_left_to_add = num_ad_with_bids - num_mismatched;

  std::vector<ProtectedAppSignalsAdWithBid> pas_ads_with_bids;
  pas_ads_with_bids.reserve(num_ad_with_bids);
  for (int i = 0; i < num_ad_with_bids; i++) {
    std::string ad_render_url;
    std::string bid_currency;

    FillInAdRenderUrlAndCurrency(matching_currency, mismatching_currency,
                                 base_ad_render_url, i, mismatched_left_to_add,
                                 matched_left_to_add, ad_render_url,
                                 bid_currency);
    pas_ads_with_bids.push_back(BuildNewPASAdWithBid(
        ad_render_url,
        /*bid_value=*/1 + 0.001 * i,
        /*enable_event_level_debug_reporting=*/false, bid_currency));
  }
  DCHECK_EQ(matched_left_to_add, 0);
  DCHECK_EQ(mismatched_left_to_add, 0);
  return pas_ads_with_bids;
}

void MockEntriesCallOnBuyerFactory(
    const google::protobuf::Map<std::string, std::string>& buyer_input,
    const BuyerFrontEndAsyncClientFactoryMock& factory) {
  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      entries;
  for (const auto& [buyer, unused] : buyer_input) {
    entries.emplace_back(buyer,
                         std::make_shared<BuyerFrontEndAsyncClientMock>());
  }

  EXPECT_CALL(factory, Entries).WillRepeatedly(Return(std::move(entries)));
}

}  // namespace privacy_sandbox::bidding_auction_servers
