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

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock-matchers.h>
#include <google/protobuf/util/message_differencer.h>
#include <include/gmock/gmock-actions.h>
#include <include/gmock/gmock-nice-strict.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "services/common/compression/gzip.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/seller_frontend_service/select_ad_reactor.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "src/cpp/communication/encoding_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::testing::Matcher;
using ::testing::Return;
using GetBidDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                GetBidsResponse::GetBidsRawResponse>>) &&>;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ScoringSignalsDoneCallback =
    absl::AnyInvocable<void(
                           absl::StatusOr<std::unique_ptr<ScoringSignals>>) &&>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = ::google::protobuf::Map<std::string, BuyerInput>;

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

void SetupBuyerClientMock(
    absl::string_view hostname,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients,
    const std::optional<GetBidsResponse::GetBidsRawResponse>& bid,
    bool repeated_get_allowed) {
  auto MockGetBids =
      [bid](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
          const RequestMetadata& metadata, GetBidDoneCallback on_done,
          absl::Duration timeout) {
        ABSL_LOG(INFO) << "Returning mock bids";
        if (bid.has_value()) {
          std::move(on_done)(
              std::make_unique<GetBidsResponse::GetBidsRawResponse>(*bid));
        }

        return absl::OkStatus();
      };
  auto SetupMockBuyer =
      [MockGetBids, repeated_get_allowed](
          std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        if (repeated_get_allowed) {
          EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(MockGetBids);
        } else {
          EXPECT_CALL(*buyer, ExecuteInternal).Times(1).WillOnce(MockGetBids);
        }
        return buyer;
      };
  auto MockBuyerFactoryCall = [SetupMockBuyer](absl::string_view hostname) {
    return SetupMockBuyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  if (repeated_get_allowed) {
    EXPECT_CALL(buyer_clients, Get(hostname))
        .WillRepeatedly(MockBuyerFactoryCall);
  } else {
    EXPECT_CALL(buyer_clients, Get(hostname)).WillOnce(MockBuyerFactoryCall);
  }
}

void BuildAdWithBidFromAdWithBidMetadata(const AdWithBidMetadata& input,
                                         AdWithBid* result) {
  if (input.has_ad()) {
    *result->mutable_ad() = input.ad();
  }
  result->set_bid(input.bid());
  result->set_render(input.render());
  result->mutable_ad_components()->CopyFrom(input.ad_components());
  result->set_allow_component_auction(input.allow_component_auction());
  result->set_interest_group_name(input.interest_group_name());
  result->set_ad_cost(kAdCost);
  result->set_modeling_signals(kModelingSignals);
}

AdWithBid BuildNewAdWithBid(const std::string& ad_url,
                            absl::optional<absl::string_view> interest_group,
                            absl::optional<float> bid_value,
                            const bool enable_event_level_debug_reporting,
                            int number_ad_component_render_urls) {
  AdWithBid bid;
  bid.set_render(ad_url);
  for (int i = 0; i < number_ad_component_render_urls; i++) {
    bid.add_ad_components(
        absl::StrCat("https://fooAds.com/adComponents?id=", i));
  }
  if (bid_value.has_value()) {
    bid.set_bid(*bid_value);
  }
  if (interest_group.has_value()) {
    bid.set_interest_group_name(*interest_group);
  }
  bid.set_ad_cost(kAdCost);
  bid.set_modeling_signals(kModelingSignals);

  if (enable_event_level_debug_reporting) {
    DebugReportUrls debug_report_urls;
    debug_report_urls.set_auction_debug_win_url(
        "https://test.com/debugWin?render=" + ad_url);
    debug_report_urls.set_auction_debug_loss_url(
        "https://test.com/debugLoss?render=" + ad_url);
    *bid.mutable_debug_report_urls() = debug_report_urls;
  }
  return bid;
}

server_common::PrivateKey GetPrivateKey() {
  server_common::PrivateKey private_key;
  private_key.key_id = std::to_string(kTestKeyId);
  private_key.private_key = GetHpkePrivateKey();
  return private_key;
}

void SetupScoringProviderMock(
    const MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>& provider,
    const BuyerBidsResponseMap& expected_buyer_bids,
    const std::optional<std::string>& scoring_signals_value,
    bool repeated_get_allowed,
    const std::optional<absl::Status>& server_error_to_return) {
  auto MockScoringSignalsProvider =
      [&expected_buyer_bids, scoring_signals_value, server_error_to_return](
          const ScoringSignalsRequest& scoring_signals_request,
          ScoringSignalsDoneCallback on_done, absl::Duration timeout) {
        EXPECT_EQ(scoring_signals_request.buyer_bids_map_.size(),
                  expected_buyer_bids.size());
        google::protobuf::util::MessageDifferencer diff;
        std::string diff_output;
        diff.ReportDifferencesToString(&diff_output);

        for (const auto& [unused, get_bid_response] : expected_buyer_bids) {
          EXPECT_TRUE(std::any_of(
              scoring_signals_request.buyer_bids_map_.begin(),
              scoring_signals_request.buyer_bids_map_.end(),
              [&diff, expected = get_bid_response.get()](auto& actual) {
                return diff.Compare(*actual.second, *expected);
              }));
        }
        if (server_error_to_return.has_value()) {
          std::move(on_done)(std::move(server_error_to_return.value()));
        } else {
          auto scoring_signals = std::make_unique<ScoringSignals>();
          if (scoring_signals_value.has_value()) {
            scoring_signals->scoring_signals =
                std::make_unique<std::string>(scoring_signals_value.value());
            std::move(on_done)(std::move(scoring_signals));
          }
        }
      };
  if (repeated_get_allowed) {
    EXPECT_CALL(provider, Get).WillRepeatedly(MockScoringSignalsProvider);
  } else {
    EXPECT_CALL(provider, Get).WillOnce(MockScoringSignalsProvider);
  }
}

TrustedServersConfigClient CreateConfig() {
  TrustedServersConfigClient config({});
  config.SetFlagForTest("1", GET_BID_RPC_TIMEOUT_MS);
  config.SetFlagForTest("2", KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS);
  config.SetFlagForTest("3", SCORE_ADS_RPC_TIMEOUT_MS);
  config.SetFlagForTest(kAuctionHost, AUCTION_SERVER_HOST);
  config.SetFlagForTest(kTrue, ENABLE_SELLER_FRONTEND_BENCHMARKING);
  config.SetFlagForTest(kSellerOriginDomain, SELLER_ORIGIN_DOMAIN);
  config.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
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
  auto ohttp_request = CreateValidEncryptedRequest(std::move(*framed_request));
  EXPECT_TRUE(ohttp_request.ok()) << ohttp_request.status().message();
  std::string encrypted_request = ohttp_request->EncapsulateAndSerialize();
  auto context = std::move(*ohttp_request).ReleaseContext();
  return {std::move(encrypted_request), std::move(context)};
}

AuctionResult DecryptAppProtoAuctionResult(
    absl::string_view auction_result_ciphertext,
    quiche::ObliviousHttpRequest::Context& context) {
  // Decrypt the response.
  auto decrypted_response =
      DecryptEncapsulatedResponse(auction_result_ciphertext, context);
  EXPECT_TRUE(decrypted_response.ok()) << decrypted_response.status().message();

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(decrypted_response->GetPlaintextData());
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();

  // Validate the error message returned in the response.
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));
  return deserialized_auction_result;
}

AuctionResult DecryptBrowserAuctionResult(
    absl::string_view auction_result_ciphertext,
    quiche::ObliviousHttpRequest::Context& context) {
  // Decrypt the response.
  auto decrypted_response =
      DecryptEncapsulatedResponse(auction_result_ciphertext, context);
  EXPECT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Decompress the encoded response.
  auto decompressed_response =
      UnframeAndDecompressAuctionResult(decrypted_response->GetPlaintextData());
  EXPECT_TRUE(decompressed_response.ok()) << decompressed_response.status();

  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  EXPECT_TRUE(deserialized_auction_result.ok())
      << deserialized_auction_result.status();
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

}  // namespace privacy_sandbox::bidding_auction_servers
