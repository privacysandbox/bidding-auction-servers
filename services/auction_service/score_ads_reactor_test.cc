//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/auction_service/score_ads_reactor.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/reporting/buyer/pa_buyer_reporting_manager.h"
#include "services/auction_service/reporting/buyer/pas_buyer_reporting_manager.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_helper_test_constants.h"
#include "services/auction_service/reporting/reporting_test_util.h"
#include "services/auction_service/reporting/seller/seller_reporting_manager.h"
#include "services/auction_service/score_ads_reactor_test_util.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/auction_service/utils/proto_utils.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/feature_flags.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/json_util.h"
#include "services/common/util/proto_util.h"
#include "src/core/test/utils/proto_test_utils.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestReportingResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"sellerLogs":["testLog"]})";
constexpr char kTestComponentReportingWinResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com&bid=2.10&modifiedBid=1.0","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"sellerLogs":["testLog"], "sellerErrors":["testLog"], "sellerWarnings":["testLog"],
"reportWinResponse":{"reportWinUrl":"http://reportWinUrl.com&bid=2.10&modifiedBid=1.0","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"buyerLogs":["testLog"], "buyerErrors":["testLog"], "buyerWarnings":["testLog"]})";
constexpr char kTestReportingWinResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"sellerLogs":["testLog"], "sellerErrors":["testLog"], "sellerWarnings":["testLog"],
"reportWinResponse":{"reportWinUrl":"http://reportWinUrl.com","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"buyerLogs":["testLog"], "buyerErrors":["testLog"], "buyerWarnings":["testLog"]})";
constexpr char kTestReportWinResponseJson[] =
    R"({"response":{"reportWinUrl":"http://reportWinUrl.com","interactionReportingUrls":{"click":"http://event.com"}},"logs":["testLog"], "errors":["testLog"], "warnings":["testLog"]})";
constexpr char kTestAdRenderUrl[] =
    "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0";
constexpr char kExpectedAuctionConfig[] =
    R"({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})";
constexpr char kExpectedPerBuyerSignals[] = R"({"testkey":"testvalue"})";
constexpr char kExpectedSignalsForWinner[] = "{testKey:testValue}";
constexpr char kEnableAdtechCodeLoggingFalse[] = "false";
constexpr int kTestDesirability = 5;
constexpr float kNotAnyOriginalBid = 1689.7;
constexpr char kTestConsentToken[] = "testConsentedToken";
constexpr char kTestEgressPayload[] = "testEgressPayload";
constexpr char kTestTemporaryEgressPayload[] = "testTemporaryEgressPayload";
constexpr char kTestComponentReportResultUrl[] =
    "http://reportResultUrl.com&bid=2.10&modifiedBid=1.0";
constexpr char kTestComponentReportWinUrl[] =
    "http://reportWinUrl.com&bid=2.10&modifiedBid=1.0";
constexpr char kUsdIsoCode[] = "USD";
constexpr char kEuroIsoCode[] = "EUR";
constexpr char kSterlingIsoCode[] = "GBP";
constexpr uint32_t kSellerDataVersion = 1989;
constexpr char kFooComponentsRenderUrl[] =
    "fooMeOnceAds.com/render_ad?id=hasFooComp";
constexpr char kTestScoreAdResponseTemplate[] = R"(
                {
                    "response": {
                      "desirability":%d,
                      "bid":1.0
                    },
                    "logs":["test log"]
                    }
)";
constexpr char kOneSellerSimpleAdScoreTemplate[] = R"(
      {
      "response" : {
        "desirability" : $0,
        "incomingBidInSellerCurrency": $1,
        "allowComponentAuction" : $2
      },
      "logs":[]
      }
)";

constexpr char kComponentWithCurrencyAdScoreTemplate[] = R"(
      {
      "response": {
        "ad": "adMetadata",
        "desirability": $0,
        "bid": $1,
        "bidCurrency": "$2",
        "incomingBidInSellerCurrency": $3,
        "allowComponentAuction": $4
      },
      "logs":[]
      }
)";

constexpr char kComponentWithCurrencyButNoIncomingAdScoreTemplate[] = R"(
      {
      "response": {
        "ad": "adMetadata",
        "desirability": $0,
        "bid": $1,
        "bidCurrency": "$2",
        "allowComponentAuction": $3
      },
      "logs":[]
      }
)";

constexpr char kComponentNoCurrencyAdScoreTemplate[] = R"(
      {
      "response" : {
        "ad": "adMetadata",
        "desirability" : $0,
        "bid" : $1,
        "incomingBidInSellerCurrency": $2,
        "allowComponentAuction": $3
      },
      "logs":[]
      }
)";

constexpr char kComponentWithCurrencyAdScore[] = R"(
      {
      "response" : {
        "ad": {"key1":"adMetadata"},
        "desirability" : 0.1,
        "bid" : 0.1,
        "bidCurrency": "USD",
        "allowComponentAuction" : true
      },
      "logs":[]
      }
)";

constexpr char kComponentWithCurrencyAndIncomingAndRejectReasAdScore[] = R"(
      {
      "response" : {
        "desirability" : $0,
        "allowComponentAuction" : $1,
        "incomingBidInSellerCurrency": $2,
        "bid" : $3,
        "bidCurrency": "$4",
        "rejectReason" : "$5"
      },
      "logs":[]
      }
)";

// Ad Scores
constexpr float kTestDesirability1 = 1.0;
constexpr float kTestDesirability2 = 2.0;

// IDs
constexpr absl::string_view kTestScoredAdDataId1 = "id1";
constexpr absl::string_view kTestScoredAdDataId2 = "id2";

// Buyer bids
constexpr float kTestBuyerBid1 = 3.0;
constexpr float kTestBuyerBid2 = 4.0;

// K-anon status
const bool kTestAnonStatusFalse = false;
const bool kTestAnonStatusTrue = true;

constexpr absl::string_view kHighScoringInterestGroupName1 = "IG1";
constexpr absl::string_view kHighScoringInterestGroupName2 = "IG2";
constexpr absl::string_view kLowScoringInterestGroupName = "IG";
constexpr absl::string_view kHighScoringInterestGroupOwner1 = "IGOwner1";
constexpr absl::string_view kHighScoringInterestGroupOwner2 = "IGOwner2";
constexpr absl::string_view kLowScoringInterestGroupOwner = "IGOwner";
constexpr absl::string_view kHighScoringInterestGroupOrigin1 = "IGOrigin1";
constexpr absl::string_view kHighScoringInterestGroupOrigin2 = "IGOrigin2";
constexpr absl::string_view kLowScoringInterestGroupOrigin = "IGOrigin";
constexpr absl::string_view kHighScoringRenderUrl1 = "RenderUrl1";
constexpr absl::string_view kHighScoringRenderUrl2 = "RenderUrl2";
constexpr absl::string_view kLowScoringRenderUrl = "RenderUrl";
constexpr absl::string_view kHighestScoringOtherBidRenderUrl = "HsobRenderUrl";
constexpr absl::string_view kHighestScoringOtherBidInterestGroupName = "HsobIG";
constexpr absl::string_view kHighestScoringOtherBidInterestGroupOrigin =
    "HsobIGOrigin";
// Bids can be used to break ties among the winners with equal scores and
// hence setting the same bid for high scored ad.
constexpr double kHighScoredBid = 1.0;
constexpr double kLowScoredBid = 3.0;
constexpr double kHighestScoringOtherBidsBidValue = 0.5;
// Winner is based on the score provided by ScoreAd.
constexpr float kHighScore = 10.0;
constexpr float kLowScore = 2.0;
constexpr float kHighestScoringOtherBidsScore = 1.0;

using ::google::protobuf::TextFormat;
using ::testing::AnyNumber;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Return;
using RawRequest = ScoreAdsRequest::ScoreAdsRawRequest;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;
using ::google::protobuf::util::MessageDifferencer;
using ::google::scp::core::test::EqualsProto;

constexpr char kInterestGroupOwnerOfBarBidder[] = "barStandardAds.com";
constexpr char kInterestGroupOrigin[] = "candy_smush";
constexpr absl::string_view kTestRenderUrl =
    "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0";
constexpr float kTestBid = 2.10;
constexpr int kTestNumOfComponentAds = 3;
constexpr absl::string_view kTestInterestGroupOwner = "https://fooAds.com";
constexpr char kTestMetadataKey[] = "arbitraryMetadataKey";
constexpr int kTestAdMetadataValue = 2;
constexpr char kTestAdComponentRenderUrlBase[] =
    "adComponent.com/foo_components/id=";
constexpr char kBarRenderUrl[] = "barStandardAds.com/render_ad?id=bar";
constexpr char kAdMetadataPropertyNameRenderUrl[] = "renderUrl";
constexpr char kAdMetadataPropertyNameMetadata[] = "metadata";
constexpr char kBarbecureIgName[] = "barbecue_lovers";

struct AdWithBidMetadataParams {
  absl::string_view render_url = kTestRenderUrl;
  const float bid = kTestBid;
  absl::string_view interest_group_name = kTestInterestGroupName;
  absl::string_view interest_group_owner = kTestInterestGroupOwner;
  absl::string_view interest_group_origin = kInterestGroupOrigin;
  const double ad_cost = kTestAdCost;
  const uint32_t data_version = kTestDataVersion;
  absl::string_view buyer_reporting_id = "";
  const int number_of_component_ads = kTestNumOfComponentAds;
  const bool k_anon_status = false;
  absl::string_view metadata_key = kTestMetadataKey;
  const int metadata_value = kTestAdMetadataValue;
  absl::string_view ad_component_render_url_base =
      kTestAdComponentRenderUrlBase;
  absl::string_view bid_currency = "";
  const bool make_metadata = true;
};

AdWithBidMetadata BuildTestAdWithBidMetadata(
    const AdWithBidMetadataParams& params = AdWithBidMetadataParams{}) {
  AdWithBidMetadata ad_with_bid_metadata;
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
  ad_with_bid_metadata.set_buyer_reporting_id(params.buyer_reporting_id);
  ad_with_bid_metadata.set_ad_cost(params.ad_cost);
  ad_with_bid_metadata.set_data_version(params.data_version);
  ad_with_bid_metadata.set_k_anon_status(params.k_anon_status);
  PS_VLOG(5) << "Generated ad bid with metadata: " << ad_with_bid_metadata;
  return ad_with_bid_metadata;
}

void PopulateTestAdWithBidMetdata(
    const PostAuctionSignals& post_auction_signals,
    const BuyerReportingDispatchRequestData& buyer_dispatch_data,
    AdWithBidMetadata& ad_with_bid_metadata) {
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
  ad_with_bid_metadata.set_interest_group_name(
      buyer_dispatch_data.interest_group_name);
  ad_with_bid_metadata.set_ad_cost(kTestAdCost);
  ad_with_bid_metadata.set_bid(post_auction_signals.winning_bid);
  ad_with_bid_metadata.set_bid_currency(kEurosIsoCode);
  ad_with_bid_metadata.set_data_version(buyer_dispatch_data.data_version);
}

AdWithBidMetadata GetTestAdWithBidSameComponentAsFoo() {
  return BuildTestAdWithBidMetadata(
      {// This must have an entry in kTestScoringSignals.
       .render_url = kFooComponentsRenderUrl,
       .interest_group_name = "foo",
       .bid_currency = kUsdIsoCode});
}

AdWithBidMetadata GetTestAdWithBidBar(
    absl::string_view buyer_reporting_id = "") {
  AdWithBidMetadata bar = BuildTestAdWithBidMetadata(
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

AdWithBidMetadata GetTestAdWithBidBarbecue() {
  const std::string render_url = "barStandardAds.com/render_ad?id=barbecue";
  AdWithBidMetadata ad_with_bid = BuildTestAdWithBidMetadata(
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

AdWithBidMetadata GetTestAdWithBidBoots() {
  std::string render_url = "bootScootin.com/render_ad?id=cowboy_boots";
  AdWithBidMetadata ad_with_bid = BuildTestAdWithBidMetadata(
      {// This must have an entry in kTestScoringSignals.
       .render_url = render_url,
       .bid = 12.15,
       .interest_group_name = "western_boot_lovers",
       .interest_group_owner = "bootScootin.com",
       .number_of_component_ads = 0,
       .make_metadata = false});

  auto* ad_map =
      ad_with_bid.mutable_ad()->mutable_struct_value()->mutable_fields();
  ad_map->try_emplace(kAdMetadataPropertyNameRenderUrl,
                      MakeAStringValue(render_url));
  ad_map->try_emplace(kAdMetadataPropertyNameMetadata,
                      MakeAListValue({
                          MakeAStringValue("french_toe"),
                          MakeAStringValue("cherry_goat"),
                          MakeAStringValue("lucchese"),
                      }));
  return ad_with_bid;
}

AdWithBidMetadata GetTestAdWithBidBarbecueWithComponents() {
  std::string render_url = "barStandardAds.com/render_ad?id=barbecue2";
  AdWithBidMetadata ad_with_bid = BuildTestAdWithBidMetadata(
      {// This must have an entry in kTestScoringSignals.
       .render_url = render_url,
       .bid = 17.76,
       .interest_group_name = kBarbecureIgName,
       .interest_group_owner = kInterestGroupOwnerOfBarBidder,
       .number_of_component_ads = 1,
       .ad_component_render_url_base = "barStandardAds.com/ad_components/id=",
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

constexpr char kTestSellerSignals[] = R"json({"seller_signal": "test 1"})json";
constexpr char kTestAuctionSignals[] =
    R"json({"auction_signal": "test 2"})json";
constexpr char kTestScoringSignals[] = R"json(
  {
    "renderUrls": {
      "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0": [
          123
        ],
      "barStandardAds.com/render_ad?id=bar": [
          "barScoringSignalValue1",
          "barScoringSignalValue2"
        ],
      "barStandardAds.com/render_ad?id=barbecue": [
          1689,
          1868
        ],
      "barStandardAds.com/render_ad?id=barbecue2": [
          1689,
          1868
        ],
      "fooMeOnceAds.com/render_ad?id=hasFooComp": [
          1689,
          1868
        ],
      "bootScootin.com/render_ad?id=cowboy_boots": [
          1689,
          1868
        ]
    },
    "adComponentRenderUrls": {
      "adComponent.com/foo_components/id=0":["foo0"],
      "adComponent.com/foo_components/id=1":["foo1"],
      "adComponent.com/foo_components/id=2":["foo2"],
      "adComponent.com/bar_components/id=0":["bar0"],
      "adComponent.com/bar_components/id=1":["bar1"],
      "adComponent.com/bar_components/id=2":["bar2"]
    }
  }
)json";
constexpr char kTestProtectedAppSignalsRenderUrl[] =
    "testAppAds.com/render_ad?id=bar";
constexpr char kTestProtectedAppScoringSignals[] = R"json(
  {
    "renderUrls": {
      "testAppAds.com/render_ad?id=bar": ["test_signal"]
    }
  }
)json";

namespace {

struct ScoreAdsRawRequestOptions {
  absl::string_view seller_signals = kTestSellerSignals;
  absl::string_view auction_signals = kTestAuctionSignals;
  absl::string_view scoring_signals = kTestScoringSignals;
  absl::string_view publisher_hostname = kTestPublisherHostName;
  const bool enable_debug_reporting = false;
  const bool enable_adtech_code_logging = false;
  absl::string_view top_level_seller = "";
  absl::string_view seller_currency = "";
  const uint32_t seller_data_version = kSellerDataVersion;
};

RawRequest BuildRawRequest(
    std::vector<AdWithBidMetadata> ads_with_bids_to_add,
    const ScoreAdsRawRequestOptions& options = ScoreAdsRawRequestOptions{}) {
  RawRequest output;
  for (int i = 0; i < ads_with_bids_to_add.size(); i++) {
    output.mutable_per_buyer_signals()->try_emplace(
        ads_with_bids_to_add[i].interest_group_owner(), kTestBuyerSignals);
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

}  // namespace

void CheckInputCorrectForFoo(std::vector<std::shared_ptr<std::string>> input,
                             const google::protobuf::Value& expected_ad) {
  auto observed_ad =
      JsonStringToValue(*input[static_cast<int>(ScoreAdArgs::kAdMetadata)]);
  CHECK_OK(observed_ad) << "Malformed observed ad JSON: "
                        << *input[static_cast<int>(ScoreAdArgs::kAdMetadata)];
  std::string difference;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  EXPECT_TRUE(differencer.Compare(*observed_ad, expected_ad))
      << "\n Observed:\n"
      << observed_ad->DebugString() << "\n\nExpected:\n"
      << expected_ad.DebugString() << "\n\nDifference:\n"
      << difference;
  EXPECT_EQ(*input[1], R"JSON(2.100000)JSON");
  EXPECT_EQ(
      *input[2],
      R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
  EXPECT_EQ(
      *input[3],
      R"JSON({"adComponentRenderUrls":{"adComponent.com/foo_components/id=0":["foo0"],"adComponent.com/foo_components/id=1":["foo1"],"adComponent.com/foo_components/id=2":["foo2"]},"renderUrl":{"https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0":[123]}})JSON");
  EXPECT_EQ(
      *input[4],
      R"JSON({"interestGroupOwner":"https://fooAds.com","topWindowHostname":"publisherName","adComponents":["adComponent.com/foo_components/id=0","adComponent.com/foo_components/id=1","adComponent.com/foo_components/id=2"],"bidCurrency":"???","dataVersion":1989,"renderUrl":"https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0"})JSON");
  EXPECT_EQ(*input[5], "{}");
}

void CheckInputCorrectForBar(std::vector<std::shared_ptr<std::string>> input,
                             const google::protobuf::Value& expected_ad) {
  auto observed_ad =
      JsonStringToValue(*input[static_cast<int>(ScoreAdArgs::kAdMetadata)]);
  CHECK_OK(observed_ad) << "Malformed observed ad JSON: "
                        << *input[static_cast<int>(ScoreAdArgs::kAdMetadata)];
  std::string difference;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  EXPECT_TRUE(differencer.Compare(*observed_ad, expected_ad))
      << "\n Observed:\n"
      << observed_ad->DebugString() << "\n\nExpected:\n"
      << expected_ad.DebugString() << "\n\nDifference:\n"
      << difference;
  EXPECT_EQ(*input[1], R"JSON(2.000000)JSON");
  EXPECT_EQ(
      *input[2],
      R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
  EXPECT_EQ(
      *input[3],
      R"JSON({"adComponentRenderUrls":{"adComponent.com/bar_components/id=0":["bar0"],"adComponent.com/bar_components/id=1":["bar1"],"adComponent.com/bar_components/id=2":["bar2"]},"renderUrl":{"barStandardAds.com/render_ad?id=bar":["barScoringSignalValue1","barScoringSignalValue2"]}})JSON");
  EXPECT_EQ(
      *input[4],
      R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisherName","adComponents":["adComponent.com/bar_components/id=0","adComponent.com/bar_components/id=1","adComponent.com/bar_components/id=2"],"bidCurrency":"USD","dataVersion":1989,"renderUrl":"barStandardAds.com/render_ad?id=bar"})JSON");
  EXPECT_EQ(*input[5], "{}");
}

void CheckInputCorrectForAdWithFooComp(
    std::vector<std::shared_ptr<std::string>> input,
    const google::protobuf::Value& expected_ad) {
  auto observed_ad =
      JsonStringToValue(*input[static_cast<int>(ScoreAdArgs::kAdMetadata)]);
  CHECK_OK(observed_ad) << "Malformed observed ad JSON: "
                        << *input[static_cast<int>(ScoreAdArgs::kAdMetadata)];
  std::string difference;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  EXPECT_EQ(*input[1], R"JSON(2.100000)JSON");
  EXPECT_EQ(
      *input[2],
      R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
  EXPECT_EQ(
      *input[3],
      R"JSON({"adComponentRenderUrls":{"adComponent.com/foo_components/id=0":["foo0"],"adComponent.com/foo_components/id=1":["foo1"],"adComponent.com/foo_components/id=2":["foo2"]},"renderUrl":{"fooMeOnceAds.com/render_ad?id=hasFooComp":[1689,1868]}})JSON");
  EXPECT_EQ(
      *input[4],
      R"JSON({"interestGroupOwner":"https://fooAds.com","topWindowHostname":"publisherName","adComponents":["adComponent.com/foo_components/id=0","adComponent.com/foo_components/id=1","adComponent.com/foo_components/id=2"],"bidCurrency":"USD","dataVersion":1989,"renderUrl":"fooMeOnceAds.com/render_ad?id=hasFooComp"})JSON");
  EXPECT_EQ(*input[5], "{}");
}

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
      .Times(AnyNumber())
      .WillRepeatedly(testing::Return(hpke_decrypt_response));

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.

  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(AnyNumber())
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
      .Times(AnyNumber())
      .WillOnce(testing::Return(aead_decrypt_response));
}

class ScoreAdsReactorTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
  ScoreAdsResponse ExecuteScoreAds(
      const RawRequest& raw_request, MockV8DispatchClient& dispatcher,
      const AuctionServiceRuntimeConfig& runtime_config,
      bool enable_report_result_url_generation = false) {
    ScoreAdsReactorTestHelper test_helper;
    return test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  }

  AuctionServiceRuntimeConfig runtime_config_;
};

TEST_F(
    ScoreAdsReactorTest,
    CreatesScoreAdInputsWithParsedScoringSignalsForTwoAdsWithSameComponents) {
  MockV8DispatchClient dispatcher;
  auto expected_foo_ad = JsonStringToValue(
      R"JSON(
      {
        "metadata": {"arbitraryMetadataKey": 2},
        "renderUrl": "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0"
      })JSON");
  CHECK_OK(expected_foo_ad) << "Malformed ad JSON";
  auto expected_foo_comp_ad_1 = JsonStringToValue(
      R"JSON(
      {
        "renderUrl": "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0",
        "metadata": {"arbitraryMetadataKey": 2}
      })JSON");
  CHECK_OK(expected_foo_comp_ad_1) << "Malformed ad JSON";
  auto expected_foo_comp_ad_2 = JsonStringToValue(
      R"JSON(
      {
        "metadata": {"arbitraryMetadataKey": 2},
        "renderUrl": "fooMeOnceAds.com/render_ad?id=hasFooComp"
      })JSON");
  CHECK_OK(expected_foo_comp_ad_2) << "Malformed ad JSON";
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce(
          [expected_foo_ad, expected_foo_comp_ad_1, expected_foo_comp_ad_2](
              std::vector<DispatchRequest>& batch,
              BatchDispatchDoneCallback done_callback) {
            EXPECT_EQ(batch.size(), 2);
            if (batch.size() == 2) {
              if (batch.at(0).id == kFooComponentsRenderUrl) {
                CheckInputCorrectForAdWithFooComp(batch.at(0).input,
                                                  *expected_foo_comp_ad_1);
                CheckInputCorrectForFoo(batch.at(1).input, *expected_foo_ad);
              } else {
                CheckInputCorrectForAdWithFooComp(batch.at(1).input,
                                                  *expected_foo_comp_ad_2);
                CheckInputCorrectForFoo(batch.at(0).input, *expected_foo_ad);
              }
            }
            return absl::OkStatus();
          });
  RawRequest raw_request = BuildRawRequest(
      {BuildTestAdWithBidMetadata(), GetTestAdWithBidSameComponentAsFoo()});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

// See b/258697130.
TEST_F(ScoreAdsReactorTest,
       CreatesScoreAdInputsInCorrectOrderWithParsedScoringSignalsForTwoAds) {
  MockV8DispatchClient dispatcher;

  auto expected_foo_ad_1 = JsonStringToValue(
      R"JSON(
      {
        "metadata": {
          "arbitraryMetadataKey": 2
        },
        "renderUrl": "fooMeOnceAds.com/render_ad?id=hasFooComp"
      })JSON");
  CHECK_OK(expected_foo_ad_1) << "Malformed ad JSON";
  auto expected_foo_ad_2 = JsonStringToValue(
      R"JSON(
    {
      "metadata": {
        "arbitraryMetadataKey": 2
      },
      "renderUrl": "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0"
    })JSON");
  CHECK_OK(expected_foo_ad_2) << "Malformed ad JSON";
  auto expected_bar_ad_1 = JsonStringToValue(
      R"JSON(
    {
      "metadata": {
        "arbitraryMetadataKey":2
      },
      "renderUrl": "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0"
    })JSON");
  CHECK_OK(expected_bar_ad_1) << "Malformed ad JSON";
  auto expected_bar_ad_2 = JsonStringToValue(
      R"JSON(
    {
      "metadata": ["140583167746", "627640802621", null, "18281019067"],
      "renderUrl": "barStandardAds.com/render_ad?id=bar"
    })JSON");
  CHECK_OK(expected_bar_ad_2) << "Malformed ad JSON";
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([expected_bar_ad_1, expected_bar_ad_2, expected_foo_ad_1,
                 expected_foo_ad_2](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 2);
        if (batch.size() == 2) {
          if (batch.at(0).id == kBarRenderUrl) {
            CheckInputCorrectForFoo(batch.at(1).input, *expected_foo_ad_2);
            CheckInputCorrectForBar(batch.at(0).input, *expected_bar_ad_2);
          } else {
            CheckInputCorrectForFoo(batch.at(0).input, *expected_foo_ad_1);
            CheckInputCorrectForBar(batch.at(1).input, *expected_bar_ad_1);
          }
        }
        return absl::OkStatus();
      });
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

// See b/258697130.
TEST_F(ScoreAdsReactorTest,
       CreatesScoreAdInputsInCorrectOrderWithParsedScoringSignals) {
  MockV8DispatchClient dispatcher;
  auto expected_foo_ad_1 = JsonStringToValue(
      R"JSON(
      {
        "metadata": {
          "arbitraryMetadataKey": 2
        },
        "renderUrl": "fooMeOnceAds.com/render_ad?id=hasFooComp"
      })JSON");
  CHECK_OK(expected_foo_ad_1) << "Malformed ad JSON";
  auto expected_foo_ad_2 = JsonStringToValue(
      R"JSON(
    {
      "metadata": {
        "arbitraryMetadataKey": 2
      },
      "renderUrl": "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0"
    })JSON");
  CHECK_OK(expected_foo_ad_2) << "Malformed ad JSON";

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([expected_foo_ad_1, expected_foo_ad_2](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback done_callback) {
        for (const auto& request : batch) {
          if (request.id == "fooMeOnceAds.com/render_ad?id=hasFooComp") {
            CheckInputCorrectForFoo(request.input, *expected_foo_ad_1);
          } else {
            CheckInputCorrectForFoo(request.input, *expected_foo_ad_2);
          }
        }
        return absl::OkStatus();
      });
  RawRequest raw_request = BuildRawRequest({BuildTestAdWithBidMetadata()});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoreAdInputsInCorrectOrderWithParsedScoringSignalsForBar) {
  MockV8DispatchClient dispatcher;
  auto expected_bar_ad_1 = JsonStringToValue(
      R"JSON(
    {
      "metadata": {
        "arbitraryMetadataKey":2
      },
      "renderUrl": "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0"
    })JSON");
  CHECK_OK(expected_bar_ad_1) << "Malformed ad JSON";
  auto expected_bar_ad_2 = JsonStringToValue(
      R"JSON(
    {
      "metadata": ["140583167746", "627640802621", null, "18281019067"],
      "renderUrl": "barStandardAds.com/render_ad?id=bar"
    })JSON");
  CHECK_OK(expected_bar_ad_2) << "Malformed ad JSON";
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([expected_bar_ad_1, expected_bar_ad_2](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback done_callback) {
        for (const auto& request : batch) {
          if (batch.at(0).id == kBarRenderUrl) {
            CheckInputCorrectForBar(request.input, *expected_bar_ad_2);
          } else {
            CheckInputCorrectForBar(request.input, *expected_bar_ad_1);
          }
        }
        return absl::OkStatus();
      });
  RawRequest raw_request = BuildRawRequest({GetTestAdWithBidBar()});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest,
       CreatesCorrectScoreAdInputsWithParsedScoringSignalsForNoComponentAds) {
  MockV8DispatchClient dispatcher;
  auto expected_ad = JsonStringToValue(
      R"JSON(
      {
        "metadata":["brisket","pulled_pork","smoked_chicken"],
        "renderUrl":"barStandardAds.com/render_ad?id=barbecue"
      })JSON");
  CHECK_OK(expected_ad) << "Malformed ad JSON";
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([expected_ad](std::vector<DispatchRequest>& batch,
                              BatchDispatchDoneCallback done_callback) {
        for (const auto& request : batch) {
          auto input = request.input;
          auto observed_ad = JsonStringToValue(
              *input[static_cast<int>(ScoreAdArgs::kAdMetadata)]);
          CHECK_OK(observed_ad)
              << "Malformed observed ad JSON: "
              << *input[static_cast<int>(ScoreAdArgs::kAdMetadata)];
          std::string difference;
          google::protobuf::util::MessageDifferencer differencer;
          differencer.ReportDifferencesToString(&difference);
          EXPECT_TRUE(differencer.Compare(*observed_ad, *expected_ad))
              << difference;
          EXPECT_EQ(*input[1], R"JSON(17.760000)JSON");
          EXPECT_EQ(
              *input[2],
              R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
          EXPECT_EQ(
              *input[3],
              R"JSON({"adComponentRenderUrls":{},"renderUrl":{"barStandardAds.com/render_ad?id=barbecue":[1689,1868]}})JSON");
          EXPECT_EQ(
              *input[4],
              R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisherName","bidCurrency":"???","dataVersion":1989,"renderUrl":"barStandardAds.com/render_ad?id=barbecue"})JSON");
        }
        return absl::OkStatus();
      });
  RawRequest raw_request = BuildRawRequest({GetTestAdWithBidBarbecue()});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoreAdInputsWellWithParsedScoringSignalsButNotForComponentAds) {
  MockV8DispatchClient dispatcher;

  auto expected_ad = JsonStringToValue(
      R"JSON(
      {
        "renderUrl": "barStandardAds.com/render_ad?id=barbecue2",
        "metadata":["brisket","pulled_pork","smoked_chicken"]
      })JSON");
  CHECK_OK(expected_ad) << "Malformed ad JSON";
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([expected_ad](std::vector<DispatchRequest>& batch,
                              BatchDispatchDoneCallback done_callback) {
        for (const auto& request : batch) {
          auto input = request.input;
          auto observed_ad = JsonStringToValue(
              *input[static_cast<int>(ScoreAdArgs::kAdMetadata)]);
          CHECK_OK(observed_ad)
              << "Malformed observed ad JSON: "
              << *input[static_cast<int>(ScoreAdArgs::kAdMetadata)];
          std::string difference;
          google::protobuf::util::MessageDifferencer differencer;
          differencer.ReportDifferencesToString(&difference);
          EXPECT_TRUE(differencer.Compare(*observed_ad, *expected_ad))
              << difference;
          EXPECT_EQ(*input[1], R"JSON(17.760000)JSON");
          EXPECT_EQ(
              *input[2],
              R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
          EXPECT_EQ(
              *input[3],
              R"JSON({"adComponentRenderUrls":{},"renderUrl":{"barStandardAds.com/render_ad?id=barbecue2":[1689,1868]}})JSON");
          EXPECT_EQ(
              *input[4],
              R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisherName","adComponents":["barStandardAds.com/ad_components/id=0"],"bidCurrency":"???","dataVersion":1989,"renderUrl":"barStandardAds.com/render_ad?id=barbecue2"})JSON");
        }
        return absl::OkStatus();
      });
  RawRequest raw_request =
      BuildRawRequest({GetTestAdWithBidBarbecueWithComponents()});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest, CanPassScoreAdUdfVersionFromRawRequest) {
  const std::string score_ad_version = "bucket/test";
  MockV8DispatchClient dispatcher;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([&score_ad_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 1);
        EXPECT_EQ(batch[0].version_string, score_ad_version);
        return absl::OkStatus();
      })
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 1);
        EXPECT_EQ(batch[0].version_string, kScoreAdBlobVersion);
        return absl::OkStatus();
      });
  RawRequest raw_request =
      BuildRawRequest({GetTestAdWithBidBarbecueWithComponents()});
  raw_request.set_score_ad_version(score_ad_version);
  ExecuteScoreAds(raw_request, dispatcher,
                  {.use_per_request_udf_versioning = true});
  ExecuteScoreAds(raw_request, dispatcher,
                  {.use_per_request_udf_versioning = false});
}

TEST_F(ScoreAdsReactorTest, CreatesScoringSignalInputPerAdWithSignal) {
  MockV8DispatchClient dispatcher;
  std::vector<AdWithBidMetadata> ads;
  AdWithBidMetadata no_signal_ad = MakeARandomAdWithBidMetadata(2, 2);
  no_signal_ad.set_render("no_signal");
  ads.push_back(no_signal_ad);
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  absl::flat_hash_set<std::string> scoring_signal_per_ad;
  for (int i = 0; i < 100; i++) {
    const AdWithBidMetadata ad = MakeARandomAdWithBidMetadata(2, 2);
    ads.push_back(ad);
    std::string ad_signal =
        absl::StrFormat("\"%s\":%s", ad.render(), R"JSON(123)JSON");

    scoring_signal_per_ad.emplace(absl::StrFormat(
        "{\"adComponentRenderUrls\":{},\"renderUrl\":{%s}}", ad_signal));
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
  }
  absl::StrAppend(&trusted_scoring_signals,
                  R"json(},"adComponentRenderUrls":{}})json");

  EXPECT_EQ(ads[0].render(), no_signal_ad.render());
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([scoring_signal_per_ad, no_signal_ad](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback done_callback) {
        for (int i = 0; i < batch.size(); i++) {
          auto& request = batch.at(i);
          auto input = request.input;
          // An ad with no signal should never make it into the execution batch.
          EXPECT_NE(no_signal_ad.render(), batch.at(i).id);
          EXPECT_TRUE(scoring_signal_per_ad.contains(*input[3]));
        }
        return absl::OkStatus();
      });

  RawRequest raw_request =
      BuildRawRequest(ads, {.scoring_signals = trusted_scoring_signals});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest, EmptySignalsResultsInNoResponse) {
  MockV8DispatchClient dispatcher;
  RawRequest raw_request = BuildRawRequest({BuildTestAdWithBidMetadata()},
                                           {.seller_signals = "",
                                            .auction_signals = "",
                                            .scoring_signals = "",
                                            .publisher_hostname = ""});
  const ScoreAdsResponse response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoresForAllAdsRequestedWithoutComponentAuction) {
  MockV8DispatchClient dispatcher;
  int current_score = 1;
  bool allowComponentAuction = false;
  AdWithBidMetadata bar = GetTestAdWithBidBar();
  AdWithBidMetadata foo = BuildTestAdWithBidMetadata();
  // Setting a seller currency requires correctly populating
  // incomingBidInSellerCurrency.
  RawRequest raw_request =
      BuildRawRequest({foo, bar}, {.seller_currency = kUsdIsoCode});

  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, &foo,
                       &bar](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback done_callback) {
        ABSL_LOG(INFO) << "Batch executing";
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          ABSL_LOG(INFO) << "Accessing id ad mapping for " << request.id;
          ++current_score;
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          ABSL_LOG(INFO) << "Successfully accessed id ad mapping for "
                         << request.id;
          score_logic.push_back(absl::Substitute(
              kOneSellerSimpleAdScoreTemplate, current_score,
              // Both bar.currency and sellerCurrency are USD,
              // so incomingBidInSellerCurrency must be unchanged,
              // or the bid will be rejected.
              (request.id == foo.render()) ? foo.bid() : bar.bid(),
              (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic));
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  ABSL_LOG(INFO) << "Accessing score to ad mapping for "
                 << scored_ad.desirability();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  ABSL_LOG(INFO) << "Accessed score to ad mapping for "
                 << scored_ad.desirability();
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.interest_group_owner(),
            original_ad_with_bid.interest_group_owner());
  EXPECT_EQ(scored_ad.interest_group_origin(),
            original_ad_with_bid.interest_group_origin());
  EXPECT_EQ(scored_ad.interest_group_origin(), kInterestGroupOrigin);
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Not a component auction, bid should not be set.
  EXPECT_FLOAT_EQ(scored_ad.bid(), 0.0f);
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming not component auctions, verify.
  EXPECT_FALSE(scored_ad.allow_component_auction());
  ASSERT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map().size(), 1);
  EXPECT_EQ(scored_ad.ad_type(), AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);
  ABSL_LOG(INFO)
      << "Accessing mapping for ig_owner_highest_scoring_other_bids_map";
  EXPECT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map()
                .at(kInterestGroupOwnerOfBarBidder)
                .values()
                .at(0)
                .number_value(),
            2);
  ABSL_LOG(INFO)
      << "Accessed mapping for ig_owner_highest_scoring_other_bids_map";
}

TEST_F(ScoreAdsReactorTest,
       RejectsBidsForFailureToMatchIncomingBidInSellerCurrency) {
  MockV8DispatchClient dispatcher;
  bool allowComponentAuction = false;
  // foo_two and bar are already in USD.
  AdWithBidMetadata foo_two, bar;
  foo_two = GetTestAdWithBidSameComponentAsFoo();
  bar = GetTestAdWithBidBar();
  // Setting seller_currency to USD requires the converting of all bids to USD.
  RawRequest raw_request =
      BuildRawRequest({bar, foo_two, GetTestAdWithBidBarbecue()},
                      {.seller_currency = kUsdIsoCode});

  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&allowComponentAuction, &score_to_ad, &id_to_ad,
                       &foo_two](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback done_callback) {
        ABSL_LOG(INFO) << "Batch executing";
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> json_ad_scores;
        int desirability_score = 1;
        for (const auto& request : batch) {
          ABSL_LOG(INFO) << "Accessing id ad mapping for " << request.id;
          score_to_ad.insert_or_assign(desirability_score,
                                       id_to_ad.at(request.id));
          ABSL_LOG(INFO) << "Successfully accessed id ad mapping for "
                         << request.id;
          json_ad_scores.push_back(absl::Substitute(
              kOneSellerSimpleAdScoreTemplate,
              // AdWithBids are fed into the script in the reverse order they
              // are added to the request, so the scores should be: { bar: 3,
              // foo_two: 2, barbecue: 1}. Thus bar is the most desirable and
              // should win, except that bar will be messed with below.
              desirability_score++,
              // Both foo_two and bar have a bid_currency of USD, and
              // sellerCurrency is USD, so for these two
              // incomingBidInSellerCurrency must be either A) unset or B)
              // unchanged, else the bid will be rejected (barbecue can have its
              // incomingBidInSellerCurrency set to anything, as it has no bid
              // currency). Setting incomingBidInSellerCurrency on bar and
              // barbecue to kNotAnyOriginalBid will thus cause rejection for
              // bar, (as bar.incomingBidInSellerCurrency does not match
              // bar.bid) whereas setting to 0 is acceptable. foo_two has a
              // lower score than bar but will win because bar will be rejected
              // for modified bid currency. barbecue will be regarded as valid
              // but lose the auction. barbecue.incomingBidInSellerCurrency will
              // be recorded as the highestScoringOtherBid.
              (request.id == foo_two.render()) ? 0.0f : kNotAnyOriginalBid,
              (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(json_ad_scores));
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  ASSERT_TRUE(raw_response.has_ad_score());
  auto scored_ad = raw_response.ad_score();

  ABSL_LOG(INFO) << "Accessing score to ad mapping for "
                 << scored_ad.desirability();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  ABSL_LOG(INFO) << "Accessed score to ad mapping for "
                 << scored_ad.desirability();

  // We expect foo_two to win the auction even though it is explicitly set to
  // the lower bid. This is because we modified the incomingBidInSellerCurrency
  // on bar.
  EXPECT_EQ(scored_ad.render(), kFooComponentsRenderUrl);
  // Desirability should be 2 (bar should have been 3 and should have been
  // rejected).
  EXPECT_FLOAT_EQ(scored_ad.desirability(), 2);

  EXPECT_FLOAT_EQ(scored_ad.buyer_bid(), foo_two.bid());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.interest_group_owner(),
            original_ad_with_bid.interest_group_owner());
  EXPECT_EQ(scored_ad.interest_group_origin(),
            original_ad_with_bid.interest_group_origin());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Not a component auction, bid should not be set.
  EXPECT_FLOAT_EQ(scored_ad.bid(), 0.0f);
  // Since in the above test we are assuming not component auctions, verify.
  EXPECT_FALSE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_type(), AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);

  // bar should have been invalidated and thus should not be recorded. barbecue
  // should have simply lost so should be recorded here.
  ASSERT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map_size(), 1);
  const auto& highest_scoring_other_bids_in_bar_standard_ads_itr =
      scored_ad.ig_owner_highest_scoring_other_bids_map().find(
          bar.interest_group_owner());
  ASSERT_NE(highest_scoring_other_bids_in_bar_standard_ads_itr,
            scored_ad.ig_owner_highest_scoring_other_bids_map().end());
  ASSERT_EQ(highest_scoring_other_bids_in_bar_standard_ads_itr->second.values()
                .size(),
            1);
  ASSERT_TRUE(
      highest_scoring_other_bids_in_bar_standard_ads_itr->second.values()[0]
          .has_number_value());
  // Since seller_currency is set, the incomingBidInSellerCurrency is recorded
  // as the bid in highestScoringOtherBid.
  ASSERT_FLOAT_EQ(
      highest_scoring_other_bids_in_bar_standard_ads_itr->second.values()[0]
          .number_value(),
      kNotAnyOriginalBid);

  // Bar should be marked as invalid.
  ASSERT_EQ(scored_ad.ad_rejection_reasons_size(), 1);
  const auto& bar_ad_rejection_reason = scored_ad.ad_rejection_reasons()[0];
  EXPECT_EQ(bar_ad_rejection_reason.interest_group_name(),
            bar.interest_group_name());
  EXPECT_EQ(bar_ad_rejection_reason.interest_group_owner(),
            bar.interest_group_owner());
  EXPECT_EQ(bar_ad_rejection_reason.rejection_reason(),
            SellerRejectionReason::INVALID_BID);
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoresForAllAdsRequestedWithHighestOtherBids) {
  MockV8DispatchClient dispatcher;
  int current_score = 1;
  bool allowComponentAuction = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()});

  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              R"(
      {
      "response" : {
        "desirability" : $0,
        "allowComponentAuction" : $1
      },
      "logs":[]
      }
)",
              current_score, (allowComponentAuction) ? "true" : "false"));
          current_score++;
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.interest_group_owner(),
            original_ad_with_bid.interest_group_owner());
  EXPECT_EQ(scored_ad.interest_group_origin(),
            original_ad_with_bid.interest_group_origin());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming not component auctions, verify.
  EXPECT_FALSE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map().size(), 1);
  EXPECT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map()
                .at(kInterestGroupOwnerOfBarBidder)
                .values()
                .at(0)
                .number_value(),
            2);
}

TEST_F(ScoreAdsReactorTest,
       IncomingBidInSellerCurrencyUsedAsHighestScoringOtherBid) {
  MockV8DispatchClient dispatcher;
  int current_score = 1;
  const float kDefaultIncomingBidInSellerCurrency = 0.92f;
  bool allowComponentAuction = false;
  // Setting a seller currency has no effect on the auction,
  // as this is not a component auction.
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.seller_currency = kEurosIsoCode});

  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, &kDefaultIncomingBidInSellerCurrency](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        ABSL_LOG(INFO) << "Batch executing";
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (auto& request : batch) {
          ABSL_LOG(INFO) << "Accessing id ad mapping for " << request.id;
          ++current_score;
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          ABSL_LOG(INFO) << "Successfully accessed id ad mapping for "
                         << request.id;
          score_logic.push_back(
              absl::Substitute(kOneSellerSimpleAdScoreTemplate, current_score,
                               // AwB.currency is USD but sellerCurrency is EUR,
                               // so incomingBidInSellerCurrency is updated.
                               kDefaultIncomingBidInSellerCurrency,
                               (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic));
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  ABSL_LOG(INFO) << "Accessing score to ad mapping for "
                 << scored_ad.desirability();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  ABSL_LOG(INFO) << "Accessed score to ad mapping for "
                 << scored_ad.desirability();
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.interest_group_owner(),
            original_ad_with_bid.interest_group_owner());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  EXPECT_EQ(scored_ad.buyer_bid_currency(),
            original_ad_with_bid.bid_currency());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming not component auctions, verify.
  EXPECT_FALSE(scored_ad.allow_component_auction());
  ASSERT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map().size(), 1);
  EXPECT_EQ(scored_ad.ad_type(), AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);
  ABSL_LOG(INFO)
      << "Accessing mapping for ig_owner_highest_scoring_other_bids_map";
  EXPECT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map()
                .at(kInterestGroupOwnerOfBarBidder)
                .values()
                .at(0)
                .number_value(),
            kDefaultIncomingBidInSellerCurrency);
  ABSL_LOG(INFO)
      << "Accessed mapping for ig_owner_highest_scoring_other_bids_map";
}

TEST_F(ScoreAdsReactorTest, PassesTopLevelSellerToComponentAuction) {
  MockV8DispatchClient dispatcher;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        for (const auto& request : batch) {
          const auto& input = request.input;
          EXPECT_EQ(
              *input[4],
              R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisherName","adComponents":["barStandardAds.com/ad_components/id=0"],"bidCurrency":"???","dataVersion":1989,"topLevelSeller":"testTopLevelSeller","renderUrl":"barStandardAds.com/render_ad?id=barbecue2"})JSON");
        }
        return absl::OkStatus();
      });
  RawRequest raw_request =
      BuildRawRequest({GetTestAdWithBidBarbecueWithComponents()},
                      {.top_level_seller = kTestTopLevelSeller});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoresForAllAdsRequestedWithComponentAuction) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = true;
  // Currency checking will take place; these two match.
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.top_level_seller = kTestTopLevelSeller,
                       .seller_currency = kUsdIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyAdScoreTemplate, current_score++,
              1 + (std::rand() % 20), kUsdIsoCode, 3,
              (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.bid_currency(), kUsdIsoCode);
  EXPECT_EQ(scored_ad.incoming_bid_in_seller_currency(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "\"adMetadata\"");
  EXPECT_GT(scored_ad.bid(), std::numeric_limits<float>::min());
}

TEST_F(ScoreAdsReactorTest,
       SetsCurrencyOnAdScorefromAdWithBidForComponentAuctionAndNoModifiedBid) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Bar's bid currency is USD
  AdWithBidMetadata bar = GetTestAdWithBidBar();
  // Currency checking will take place since seller_currency set.
  // No modified bid will be set, so the original bid currency will be checked.
  RawRequest raw_request =
      BuildRawRequest({bar}, {.top_level_seller = kTestTopLevelSeller,
                              .seller_currency = kUsdIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad,
                       &bar](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(
              absl::Substitute(kComponentNoCurrencyAdScoreTemplate,
                               // NOLINTNEXTLINE
                               /*desirability=*/current_score++,
                               // This is set to 0 so the buyer's (original) bid
                               // will be used. NOLINTNEXTLINE
                               /*(modified) bid=*/0,
                               // Since seller_currency is set to USD, and bar's
                               // currency is USD, this needs to match the
                               // original bid, else the AdScore will be
                               // rejected. NOLINTNEXTLINE
                               /*incomingBidInSellerCurrency=*/bar.bid(),
                               (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  // Expect USD because original AdWithBid's currency is USD.
  EXPECT_EQ(scored_ad.bid_currency(), kUsdIsoCode);
  // Bar's bid was already in USD, and the seller currency is USD, so it must be
  // bar.bid().
  EXPECT_EQ(scored_ad.incoming_bid_in_seller_currency(), bar.bid());
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "\"adMetadata\"");
  // Modified bid set to 0, so buyer_bid should be used.
  EXPECT_EQ(scored_ad.bid(), bar.bid());
}

TEST_F(ScoreAdsReactorTest, BidRejectedForModifiedIncomingBidInSellerCurrency) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Bar's bid currency is USD
  AdWithBidMetadata bar = GetTestAdWithBidBar();
  // Seller currency set to USD.
  // Currency checking will take place since seller_currency is set.
  RawRequest raw_request =
      BuildRawRequest({bar}, {.top_level_seller = kTestTopLevelSeller,
                              .seller_currency = kUsdIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad,
                       &bar](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentNoCurrencyAdScoreTemplate,
              // NOLINTNEXTLINE
              /*desirability=*/current_score++,
              // This is set to 0 so the buyer's (original) bid will be used.
              /*(modified) bid=*/0,
              // Since seller_currency is set to USD, and bar's currency is USD,
              // this needs to match the original bid, else the AdScore
              // will be rejected.
              // We are deliberately setting it wrong so it is rejected.
              // NOLINTNEXTLINE
              /*incomingBidInSellerCurrency=*/bar.bid() + 1,
              (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest,
       BidWithMismatchedModifiedCurrencyAcceptedSinceModifiedBidNotSet) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Bar's bid currency is USD
  AdWithBidMetadata bar = GetTestAdWithBidBar();
  // Seller currency omitted so currency checking will NOT take place.
  RawRequest raw_request =
      BuildRawRequest({bar}, {.top_level_seller = kTestTopLevelSeller,
                              .seller_currency = kUsdIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad,
                       &bar](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyAdScoreTemplate,
              // NOLINTNEXTLINE
              /*desirability=*/current_score++,
              // This is set to 0 so the buyer's (original) bid will be used.
              /*(modified) bid=*/0,
              // Setting this to EUR would normally fail currency checking,
              // but in this case the adScore will pass
              // because EUR will be IGNORED as it is the currency
              // of the modified bid, and the modified bid was not set.
              // NOLINTNEXTLINE
              /*bidCurrency=*/kEuroIsoCode,
              // Since seller_currency is set, this must match the original bid.
              // NOLINTNEXTLINE
              /*incomingBidInSellerCurrency=*/bar.bid(),
              (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  // Expect USD because original AdWithBid's currency is USD.
  EXPECT_EQ(scored_ad.bid_currency(), kUsdIsoCode);
  // USD->USD means no translation.
  EXPECT_EQ(scored_ad.incoming_bid_in_seller_currency(), bar.bid());
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "\"adMetadata\"");
  // Modified bid not set, so buyer_bid used.
  EXPECT_EQ(scored_ad.bid(), bar.bid());
}

TEST_F(ScoreAdsReactorTest, BidCurrencyCanBeModifiedWhenNoSellerCurrencySet) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Seller currency omitted so currency checking will NOT take place.
  RawRequest raw_request = BuildRawRequest(
      {GetTestAdWithBidBar()}, {.top_level_seller = kTestTopLevelSeller});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyAdScoreTemplate,
              // NOLINTNEXTLINE
              /*desirability=*/current_score++,
              // This is set to a positive and nonzero value so it will be used.
              /*(modified) bid=*/1.689,
              // Setting this to EUR is fine as no seller_currency is set.
              // NOLINTNEXTLINE
              /*bidCurrency=*/kEuroIsoCode,
              // Since seller_currency is not set, this is ignored.
              // NOLINTNEXTLINE
              /*incomingBidInSellerCurrency=*/1.776,
              (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_FLOAT_EQ(scored_ad.bid(), 1.689);
  // Expect EUR even though original AdWithBid's currency is USD, as this is
  // what scoreAd() chose to set the currency to.
  EXPECT_EQ(scored_ad.bid_currency(), kEuroIsoCode);
  EXPECT_FLOAT_EQ(scored_ad.incoming_bid_in_seller_currency(), 1.776);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "\"adMetadata\"");
}

/**
 * What's going on in this test?
 * - This is a component auction.
 * - The bids coming out of scoreAd() have no set (modified) bid.
 * - Therefore their original buyer bids will be used.
 * - Therefore the currency of those original buyer bids will be used.
 * - As with all component auctions, any adScore with a set currency must
 * undergo currency checking with the seller_currency.
 * - The seller currency set below is EUR.
 * - The AdWithBid used is bar, which has a currency of USD.
 * - So the original buyer_bid will be set on the AdScore as the modified bid,
 * and the original buyer_bid_currency (USD) will be set on the AdScore.
 * - USD is not EUR, so the AdScore will be rejected.
 */
TEST_F(ScoreAdsReactorTest,
       AdScoreRejectedForMismatchedCurrencyWhenBuyerBidCurrencyUsed) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Bar's bid currency is USD
  // Currency checking will take place since seller_currency set.
  // No modified bid will be set, so the original bid currency will be checked.
  // This will cause a mismatch and a rejection.
  RawRequest raw_request = BuildRawRequest(
      {GetTestAdWithBidBar()}, {.top_level_seller = kTestTopLevelSeller,
                                .seller_currency = kEurosIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentNoCurrencyAdScoreTemplate,
              // NOLINTNEXTLINE
              /*desirability=*/current_score++,
              // This is set to 0 so the buyer's (original) bid will be used.
              /*(modified) bid=*/0,
              // Since seller_currency is set to EUR, and bar's currency is USD,
              // scoreAd() is responsible for setting this correctly,
              // but B&A can't verify that it is set correctly.
              // NOLINTNEXTLINE
              /*incomingBidInSellerCurrency=*/1.776,
              (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest, ModifiedBidOnScoreRejectedForCurrencyMismatch) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = true;
  // EUR will not match the AdScore.bid_currency of USD.
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.top_level_seller = kTestTopLevelSeller,
                       .seller_currency = kEuroIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyButNoIncomingAdScoreTemplate,
              // USD will not match the seller_currency of EUR.
              current_score++, 1 + (std::rand() % 20), kUsdIsoCode,
              (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest, ParsesJsonAdMetadataInComponentAuction) {
  MockV8DispatchClient dispatcher;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.top_level_seller = kTestTopLevelSeller});

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic(batch.size(),
                                             kComponentWithCurrencyAdScore);
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()))
      << "ScoreAdsResponse could not be parsed: "
      << response.response_ciphertext();
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.ad_metadata(), "{\"key1\":\"adMetadata\"}");
}

TEST_F(ScoreAdsReactorTest, CreatesDebugUrlsForAllAds) {
  MockV8DispatchClient dispatcher;
  bool allowComponentAuction = false;
  // Setting seller currency will not trigger currency checking as the AdScores
  // have no currency.
  RawRequest raw_request = BuildRawRequest(
      {BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
      {.enable_debug_reporting = true, .seller_currency = kUsdIsoCode});

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly(
          [&allowComponentAuction](std::vector<DispatchRequest>& batch,
                                   BatchDispatchDoneCallback done_callback) {
            // Each original ad request (AdWithBidMetadata) is stored by its
            // expected score and later compared to the output AdScore with the
            // matching score.
            std::vector<std::string> score_logic;
            score_logic.reserve(batch.size());
            for (int current_score = 0; current_score < batch.size();
                 ++current_score) {
              score_logic.push_back(absl::StrCat(
                  "{\"response\":"
                  "{\"desirability\": ",
                  current_score, ", \"bid\": ", 1 + (std::rand() % 20),
                  ", \"allowComponentAuction\": ",
                  ((allowComponentAuction) ? "true" : "false"),
                  ", \"debugReportUrls\": {",
                  "    \"auctionDebugLossUrl\" : "
                  "\"https://example-ssp.com/debugLoss\",",
                  "    \"auctionDebugWinUrl\" : "
                  "\"https://example-ssp.com/debugWin\"",
                  "}}, \"logs\":[]}"));
            }
            return FakeExecute(batch, std::move(done_callback),
                               std::move(score_logic), true, true);
          });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_TRUE(scored_ad.has_debug_report_urls());
  EXPECT_EQ(scored_ad.debug_report_urls().auction_debug_loss_url(),
            "https://example-ssp.com/debugLoss");
  EXPECT_EQ(scored_ad.debug_report_urls().auction_debug_win_url(),
            "https://example-ssp.com/debugWin");
}

TEST_F(ScoreAdsReactorTest, SuccessExecutesInRomaWithLogsEnabled) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.enable_debug_reporting = true});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(
              absl::StrCat("{\"response\":{\"desirability\": ", current_score++,
                           ", \"bid\": ", 1 + (std::rand() % 20),
                           ", \"allowComponentAuction\": ",
                           ((allowComponentAuction) ? "true" : "false"),
                           ", \"debugReportUrls\": {",
                           "    \"auctionDebugLossUrl\" : "
                           "\"https://example-ssp.com/debugLoss\",",
                           "    \"auctionDebugWinUrl\" : "
                           "\"https://example-ssp.com/debugWin\"",
                           "}},"
                           "\"logs\":[\"test log\"],"
                           "\"errors\":[\"test error\"],"
                           "\"warnings\":[\"test warning\"]"
                           "}"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true, true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true,
      .enable_adtech_code_logging = true};
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
}

// [[deprecated]]
TEST_F(ScoreAdsReactorTest, SuccessfullyExecutesReportResult) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  bool enable_adtech_code_logging = false;
  bool enable_report_result_url_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            response.emplace_back(kTestReportingResponseJson);
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation};
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

TEST_F(ScoreAdsReactorTest,
       SellerReportingUrlsEmptyWhenParsingUdfResponseFails) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  bool enable_adtech_code_logging = false;
  bool enable_report_result_url_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportResultEntryFunction) == 0) {
            response.emplace_back(kTestReportingResponseJson);
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(
      kTestInterestGroupOwner.data());
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_TRUE(scored_ad.win_reporting_urls()
                  .top_level_seller_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
}

TEST_F(ScoreAdsReactorTest,
       SuccessfullyExecutesReportResultAndReportWinForComponentAuctions) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = true;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_result_win_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.enable_adtech_code_logging = true,
                       .top_level_seller = kTestTopLevelSeller});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id(render id) is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting,
                       enable_report_result_win_generation](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            if (enable_report_result_win_generation) {
              response.emplace_back(kTestComponentReportingWinResponseJson);
            } else {
              response.emplace_back(kTestReportingResponseJson);
            }
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "ad":"adMetadata",
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1.0,
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_result_win_generation};
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestComponentReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestComponentReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

// [[deprecated]]
TEST_F(ScoreAdsReactorTest, SuccessfullyExecutesReportResultAndReportWin) {
  absl::SetFlag(&FLAGS_enable_temporary_unlimited_egress, true);
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_result_win_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.enable_adtech_code_logging = true});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting,
                       enable_report_result_win_generation](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            if (enable_report_result_win_generation) {
              response.emplace_back(kTestReportingWinResponseJson);
            } else {
              response.emplace_back(kTestReportingResponseJson);
            }
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_result_win_generation};
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

// [[deprecated]]
TEST_F(ScoreAdsReactorTest, ReportingUrlsAndBuyerReportingIdSetInAdScore) {
  absl::SetFlag(&FLAGS_enable_temporary_unlimited_egress, true);
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_result_win_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(
                           {.buyer_reporting_id = kTestBuyerReportingId}),
                       GetTestAdWithBidBar(kTestBuyerReportingId)},
                      {.enable_adtech_code_logging = true});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting,
                       enable_report_result_win_generation](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            if (enable_report_result_win_generation) {
              response.emplace_back(kTestReportingWinResponseJson);
            } else {
              response.emplace_back(kTestReportingResponseJson);
            }
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_result_win_generation};
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.buyer_reporting_id(), kTestBuyerReportingId);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

// [[deprecated]]
TEST_F(ScoreAdsReactorTest, ReportResultFailsReturnsOkayResponse) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  bool enable_adtech_code_logging = false;
  bool enable_report_result_url_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            std::vector<absl::StatusOr<DispatchResponse>> responses;
            done_callback(responses);
            return absl::ErrnoToStatus(1, "Unknown error");
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation};
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            "");
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
}

TEST_F(ScoreAdsReactorTest, IgnoresUnknownFieldsFromScoreAdResponse) {
  MockV8DispatchClient dispatcher;
  RawRequest raw_request = BuildRawRequest({BuildTestAdWithBidMetadata()},
                                           {.enable_debug_reporting = true});
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        score_logic.push_back(absl::Substitute(R"(
      {
      "response" : {
        "desirability" : 1,
        "bid" : $0,
        "unknownFieldKey" : 0
      },
      "logs":[]
      }
)",
                                               1 + (std::rand() % 20)));
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  auto scored_ad = raw_response.ad_score();
  // Ad was parsed successfully.
  EXPECT_EQ(scored_ad.desirability(), 1);
}

TEST_F(ScoreAdsReactorTest, VerifyDecryptionEncryptionSuccessful) {
  MockV8DispatchClient dispatcher;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        score_logic.push_back(absl::Substitute(R"(
      {
      "response" : {
        "desirability" : 1,
        "bid" : $0,
        "unknownFieldKey" : 0
      },
      "logs":[]
      }
)",
                                               1 + (std::rand() % 20)));
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  RawRequest raw_request = BuildRawRequest({BuildTestAdWithBidMetadata()});

  ScoreAdsResponse response;
  ScoreAdsRequest request;
  request.set_key_id("key_id");
  request.set_request_ciphertext("ciphertext");
  std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger =
      std::make_unique<ScoreAdsNoOpLogger>();

  // Return an empty key.
  server_common::MockKeyFetcherManager key_fetcher_manager;
  server_common::PrivateKey private_key;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillOnce(testing::Return(private_key));

  MockCryptoClientWrapper crypto_client;
  // Mock the HpkeDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse decrypt_response;
  decrypt_response.set_payload(raw_request.SerializeAsString());
  decrypt_response.set_secret("secret");
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .WillOnce(testing::Return(decrypt_response));

  // Mock the AeadEncrypt() call on the crypto_client. This is used for
  // encrypting the response.
  google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
  data.set_ciphertext("ciphertext");
  google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse encrypt_response;
  *encrypt_response.mutable_encrypted_data() = std::move(data);
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .WillOnce(testing::Return(encrypt_response));

  SetupTelemetryCheck(request);

  // Mock Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  grpc::CallbackServerContext context;
  ScoreAdsReactor reactor(&context, dispatcher, &request, &response,
                          std::move(benchmarkingLogger), &key_fetcher_manager,
                          &crypto_client, async_reporter.get(),
                          runtime_config_);
  reactor.Execute();

  EXPECT_FALSE(response.response_ciphertext().empty());
}

// This test case validates that reject reason is returned in response of
// ScoreAds for ads where it was of any valid value other than 'not-available'.
TEST_F(ScoreAdsReactorTest, CaptureRejectionReasonsForRejectedAds) {
  MockV8DispatchClient dispatcher;
  bool allowComponentAuction = true;
  // Each ad must have its own unique render url, lest the IDs clash during roma
  // batch execution and one gets clobbered.
  AdWithBidMetadata foo, bar, barbecue, boots;
  // AdWithBid Foo is to be rejected by the scoreAd() code, which will delcare
  // it invalid.
  foo = BuildTestAdWithBidMetadata();
  // AdWithBid Bar has bid currency USD. Since this matches the USD set as the
  // seller currency below, the AdScore for bar must have an
  // incomingBidInSellerCurrency which is unmodified (that is, which matches
  // bar.bid()). This test will deliberately violate this requirement below to
  // test the seller rejection reason setting.
  bar = GetTestAdWithBidBar();
  // This AdWithBid is intended to be the sole non-rejected AwB.
  barbecue = GetTestAdWithBidBarbecue();
  // Boots's modified bid currency of Sterling should clash with the stated
  // auction currency of USD below, leading to a seller rejection reason of
  // currency mismatch.
  boots = GetTestAdWithBidBoots();

  RawRequest raw_request = BuildRawRequest(
      {foo, bar, barbecue, boots}, {.top_level_seller = kTestTopLevelSeller,
                                    .seller_currency = kUsdIsoCode});

  ASSERT_EQ(raw_request.ad_bids_size(), 4);

  absl::flat_hash_map<std::string, std::string> id_to_rejection_reason;
  // Make sure id is tracked to render urls to stay unique.
  // Bar is to have its rejection reason assigned by the reactor code for
  // incomingBidInSellerCurrency illegal modification.
  id_to_rejection_reason.insert_or_assign(bar.render(), "not-available");
  id_to_rejection_reason.insert_or_assign(foo.render(), "invalid-bid");
  // barbecue is to survive unscathed and should win the auction.
  id_to_rejection_reason.insert_or_assign(barbecue.render(), "not-available");
  id_to_rejection_reason.insert_or_assign(boots.render(), "not-available");

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&allowComponentAuction, &id_to_rejection_reason, &bar,
                       &boots](std::vector<DispatchRequest>& batch,
                               BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          std::string rejection_reason = id_to_rejection_reason.at(request.id);
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyAndIncomingAndRejectReasAdScore,
              // Disable this check because all args to
              // absl::Substitute() are named generically, so explicitly calling
              // out "desirability" and "bid" runs afoul of these checks.
              // NOLINTNEXTLINE
              /*desirability=*/1, (allowComponentAuction) ? "true" : "false",
              // IG Bar's bid currency is USD. As Seller currency is USD, bar's
              // incomingBidInSellerCurrency must equal the original bid or the
              // bid should be rejected for invalid incomingBidInSellerCurrency.
              // So here, we deliberately set
              // barAdScore.incomingBidInSellerCurrency to NOT bar.bid(), so
              // that bar is rejected and we can test its reject reason. The
              // other AdScores just don't have incomingBidInSellerCurrency set
              // on them (0 is default).
              (request.id == bar.render()) ? bar.bid() + 1689 : 0.0f,
              // NOLINTNEXTLINE
              /*bid=*/1 + (std::rand() % 20),
              // Only the boots AwB shall be rejected for currency mismatch.
              // NOLINTNEXTLINE
              /*bidCurrency=*/
              (request.id == boots.render()) ? kSterlingIsoCode : "",
              rejection_reason));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  auto scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Assertion avoids out-of-bounds array accesses below.
  ASSERT_EQ(scored_ad.ad_rejection_reasons().size(), 3);

  int num_matching = 0;
  // Note that the batch responses can be in any order.
  for (const auto& ad_rejection_reason : scored_ad.ad_rejection_reasons()) {
    PS_VLOG(5) << "Found ad rejection reason: "
               << ad_rejection_reason.DebugString();
    if (ad_rejection_reason.interest_group_name() ==
        boots.interest_group_name()) {
      ++num_matching;
      EXPECT_EQ(ad_rejection_reason.interest_group_owner(),
                boots.interest_group_owner());
      EXPECT_EQ(ad_rejection_reason.rejection_reason(),
                SellerRejectionReason::BID_FROM_SCORE_AD_FAILED_CURRENCY_CHECK);
    } else if (ad_rejection_reason.interest_group_name() ==
               bar.interest_group_name()) {
      ++num_matching;
      EXPECT_EQ(ad_rejection_reason.interest_group_owner(),
                bar.interest_group_owner());
      EXPECT_EQ(ad_rejection_reason.rejection_reason(),
                SellerRejectionReason::INVALID_BID);
    } else if (ad_rejection_reason.interest_group_name() ==
               foo.interest_group_name()) {
      ++num_matching;
      EXPECT_EQ(ad_rejection_reason.interest_group_owner(),
                foo.interest_group_owner());
      EXPECT_EQ(ad_rejection_reason.rejection_reason(),
                SellerRejectionReason::INVALID_BID);
    }
  }
  ASSERT_EQ(num_matching, 3);
}

TEST_F(ScoreAdsReactorTest,
       ScoredAdRemovedFromConsiderationWhenRejectReasonAvailable) {
  MockV8DispatchClient dispatcher;
  RawRequest raw_request = BuildRawRequest({BuildTestAdWithBidMetadata()});
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic(batch.size(),
                                             R"(
                  {
                  "response" : {
                    "desirability" : 1000,
                    "bid" : 1000,
                    "allowComponentAuction" : false,
                    "rejectReason" : "invalid-bid"
                  },
                  "logs":[]
                  })");
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));

  // No ad is present in the response even though the scored ad had non-zero
  // desirability.
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest, ZeroDesirabilityAdsConsideredAsRejected) {
  MockV8DispatchClient dispatcher;
  RawRequest raw_request = BuildRawRequest({BuildTestAdWithBidMetadata()});
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic(batch.size(),
                                             R"(
          {
            "response" : 0
          })");
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));

  // No ad is present in the response when a zero desirability is returned
  // for the scored ad.
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest, SingleNumberResponseFromScoreAdIsValid) {
  MockV8DispatchClient dispatcher;
  RawRequest raw_request = BuildRawRequest({BuildTestAdWithBidMetadata()},
                                           {.enable_debug_reporting = true});
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        score_logic.push_back(absl::Substitute(R"(
      {
      "response" : 1.5
      }
)"));
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  auto scored_ad = raw_response.ad_score();
  // Ad was parsed successfully.
  EXPECT_DOUBLE_EQ(scored_ad.desirability(), 1.5);
  EXPECT_FALSE(scored_ad.allow_component_auction());
}

RawRequest BuildProtectedAppSignalsRawRequest(
    std::vector<ProtectedAppSignalsAdWithBidMetadata> ads_with_bids_to_add,
    const ScoreAdsRawRequestOptions& options = ScoreAdsRawRequestOptions{}) {
  RawRequest output;
  for (int i = 0; i < ads_with_bids_to_add.size(); i++) {
    output.mutable_per_buyer_signals()->try_emplace(
        ads_with_bids_to_add[i].owner(), kTestBuyerSignals);
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

ProtectedAppSignalsAdWithBidMetadata GetProtectedAppSignalsAdWithBidMetadata(
    absl::string_view render_url, float bid = kTestBid) {
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

class ScoreAdsReactorProtectedAppSignalsTest : public ScoreAdsReactorTest {
 protected:
  void SetUp() override {
    runtime_config_.enable_protected_app_signals = true;
    server_common::log::SetGlobalPSVLogLevel(10);
  }

  ScoreAdsResponse ExecuteScoreAds(const RawRequest& raw_request,
                                   MockV8DispatchClient& dispatcher) {
    return ScoreAdsReactorTest::ExecuteScoreAds(raw_request, dispatcher,
                                                runtime_config_);
  }

  AuctionServiceRuntimeConfig runtime_config_;
};

TEST_F(ScoreAdsReactorProtectedAppSignalsTest,
       AdDispatchedForScoringWhenSignalsPresent) {
  MockV8DispatchClient dispatcher;
  auto expected_ad = JsonStringToValue(
      R"JSON(
      {
        "metadata": {"arbitraryMetadataKey":2},
        "renderUrl": "testAppAds.com/render_ad?id=bar"
      })JSON");
  CHECK_OK(expected_ad) << "Malformed ad JSON";
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([expected_ad](std::vector<DispatchRequest>& batch,
                              BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 1);
        const auto& req = batch[0];
        EXPECT_EQ(req.handler_name, "scoreAdEntryFunction");

        EXPECT_EQ(req.input.size(), 7);
        auto observed_ad = JsonStringToValue(
            *req.input[static_cast<int>(ScoreAdArgs::kAdMetadata)]);
        CHECK_OK(observed_ad);
        std::string difference;
        google::protobuf::util::MessageDifferencer differencer;
        differencer.ReportDifferencesToString(&difference);
        EXPECT_TRUE(differencer.Compare(*observed_ad, *expected_ad))
            << difference;
        EXPECT_THAT(*req.input[static_cast<int>(ScoreAdArgs::kBid)],
                    HasSubstr(absl::StrCat(kTestBid)));
        EXPECT_EQ(*req.input[static_cast<int>(ScoreAdArgs::kAuctionConfig)],
                  "{\"auctionSignals\": {\"auction_signal\": \"test 2\"}, "
                  "\"sellerSignals\": {\"seller_signal\": \"test 1\"}}");
        EXPECT_EQ(*req.input[static_cast<int>(ScoreAdArgs::kScoringSignals)],
                  "{\"renderUrl\":{\"testAppAds.com/"
                  "render_ad?id=bar\":[\"test_signal\"]}}");
        EXPECT_EQ(
            *req.input[static_cast<int>(ScoreAdArgs::kBidMetadata)],
            R"JSON({"interestGroupOwner":"https://PAS-Ad-Owner.com","topWindowHostname":"publisherName","bidCurrency":"USD","renderUrl":"testAppAds.com/render_ad?id=bar"})JSON");
        EXPECT_EQ(
            *req.input[static_cast<int>(ScoreAdArgs::kDirectFromSellerSignals)],
            "{}");
        EXPECT_EQ(*req.input[static_cast<int>(ScoreAdArgs::kFeatureFlags)],
                  "{\"enable_logging\": false,\"enable_debug_url_generation\": "
                  "false}");
        return absl::OkStatus();
      });
  ProtectedAppSignalsAdWithBidMetadata ad =
      GetProtectedAppSignalsAdWithBidMetadata(kTestProtectedAppSignalsRenderUrl,
                                              kTestBid);
  RawRequest raw_request = BuildProtectedAppSignalsRawRequest(
      {std::move(ad)}, {.scoring_signals = kTestProtectedAppScoringSignals});
  ExecuteScoreAds(raw_request, dispatcher);
}

TEST_F(ScoreAdsReactorProtectedAppSignalsTest,
       NoDispatchWhenThereAreBidsButFeatureDisabled) {
  runtime_config_.enable_protected_app_signals = false;
  MockV8DispatchClient dispatcher;
  EXPECT_CALL(dispatcher, BatchExecute).Times(0);
  ProtectedAppSignalsAdWithBidMetadata ad =
      GetProtectedAppSignalsAdWithBidMetadata(kTestProtectedAppSignalsRenderUrl,
                                              kTestBid);
  RawRequest raw_request = BuildProtectedAppSignalsRawRequest(
      {std::move(ad)}, {.scoring_signals = kTestProtectedAppScoringSignals});
  ExecuteScoreAds(raw_request, dispatcher);
}

TEST_F(ScoreAdsReactorProtectedAppSignalsTest,
       AdNotDispatchedForScoringWhenSignalsAbsent) {
  MockV8DispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute).Times(0);
  ProtectedAppSignalsAdWithBidMetadata ad =
      GetProtectedAppSignalsAdWithBidMetadata(
          kTestProtectedAppSignalsRenderUrl);
  RawRequest raw_request = BuildProtectedAppSignalsRawRequest(
      {std::move(ad)}, {.scoring_signals = R"JSON({"renderUrls": {}})JSON"});
  ExecuteScoreAds(raw_request, dispatcher);
}

// [[deprecated]]
TEST_F(ScoreAdsReactorProtectedAppSignalsTest,
       SuccessfullyExecutesReportResultAndReportWin) {
  MockV8DispatchClient dispatcher;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_result_win_generation = true;
  bool enable_debug_reporting = false;
  ProtectedAppSignalsAdWithBidMetadata protected_app_signals_ad_with_bid =
      GetProtectedAppSignalsAdWithBidMetadata(
          kTestProtectedAppSignalsRenderUrl);
  RawRequest raw_request = BuildProtectedAppSignalsRawRequest(
      {std::move(protected_app_signals_ad_with_bid)},
      {.scoring_signals = kTestProtectedAppScoringSignals});
  {
    InSequence s;
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([](std::vector<DispatchRequest>& batch,
                     BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          PS_LOG(INFO) << "UDF handler: " << request.handler_name;
          EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
          auto incoming_bid =
              request.input[static_cast<int>(ScoreAdArgs::kBid)];
          float bid;
          EXPECT_TRUE(absl::SimpleAtof(*incoming_bid, &bid))
              << "Failed to convert bid to float: " << *incoming_bid;
          std::vector<absl::StatusOr<DispatchResponse>> responses;
          DispatchResponse response = {.id = request.id,
                                       .resp = absl::StrFormat(
                                           R"JSON(
                  {
                    "response": {
                      "desirability":%d,
                      "bid":%f
                    },
                    "logs":["test log"]
                    }
                  )JSON",
                                           kTestDesirability, bid)};
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillRepeatedly([enable_report_result_win_generation](
                            std::vector<DispatchRequest>& batch,
                            BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          PS_LOG(INFO) << "UDF handler: " << request.handler_name;
          EXPECT_EQ(request.handler_name,
                    kReportingProtectedAppSignalsFunctionName);
          EXPECT_EQ(
              *request.input[ReportingArgIndex(ReportingArgs::kEgressPayload)],
              absl::StrCat("\"", kTestEgressPayload, "\""));
          EXPECT_EQ(*request.input[ReportingArgIndex(
                        ReportingArgs::kTemporaryEgressPayload)],
                    absl::StrCat("\"", kTestTemporaryEgressPayload, "\""));
          DispatchResponse response;
          if (enable_report_result_win_generation) {
            response.resp = kTestReportingWinResponseJson;
          } else {
            response.resp = kTestReportingResponseJson;
          }
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
  }
  runtime_config_ = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_result_win_generation,
      .enable_protected_app_signals = true};
  const auto& response = ExecuteScoreAds(raw_request, dispatcher);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), kTestDesirability);
  EXPECT_EQ(scored_ad.render(), kTestProtectedAppSignalsRenderUrl);
  EXPECT_EQ(scored_ad.component_renders().size(), 0);
  EXPECT_EQ(scored_ad.interest_group_name(), "");
  EXPECT_EQ(scored_ad.interest_group_owner(), kTestProtectedAppSignalsAdOwner);
  EXPECT_EQ(scored_ad.buyer_bid(), kTestBid);
  EXPECT_FALSE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_type(), AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD);

  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

void VerifyReportWinInput(
    const DispatchRequest& request, absl::string_view buyer_version,
    const SellerReportingDispatchRequestData& seller_dispatch_data,
    const BuyerReportingDispatchRequestData& buyer_dispatch_data) {
  EXPECT_EQ(request.handler_name, kReportWinEntryFunction);
  EXPECT_EQ(request.version_string, buyer_version);
  EXPECT_EQ(
      *request.input[PAReportWinArgIndex(PAReportWinArgs::kAuctionConfig)],
      kExpectedAuctionConfig);
  EXPECT_EQ(
      *request.input[PAReportWinArgIndex(PAReportWinArgs::kPerBuyerSignals)],
      kExpectedPerBuyerSignals);
  EXPECT_EQ(
      *request.input[PAReportWinArgIndex(PAReportWinArgs::kEnableLogging)],
      kEnableAdtechCodeLoggingFalse);
  EXPECT_EQ(
      *request.input[PAReportWinArgIndex(PAReportWinArgs::kSignalsForWinner)],
      kExpectedSignalsForWinner);
  VerifyPABuyerReportingSignalsJson(
      *request
           .input[PAReportWinArgIndex(PAReportWinArgs::kBuyerReportingSignals)],
      buyer_dispatch_data, seller_dispatch_data);
}

void VerifyBuyerReportingUrl(const ScoreAdsResponse::AdScore& scored_ad) {
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

void VerifyReportingUrlsInScoreAdsResponse(
    const ScoreAdsResponse::ScoreAdsRawResponse& raw_response) {
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), kTestDesirability);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  VerifyBuyerReportingUrl(scored_ad);
}

void VerifyReportingUrlsInScoreAdsResponseForComponentAuction(
    const ScoreAdsResponse::ScoreAdsRawResponse& raw_response) {
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), kTestDesirability);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  VerifyBuyerReportingUrl(scored_ad);
}

void VerifyOnlyReportResultUrlInScoreAdsResponse(
    const ScoreAdsResponse::ScoreAdsRawResponse& raw_response) {
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), kTestDesirability);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_TRUE(scored_ad.win_reporting_urls()
                  .buyer_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
}

TEST_F(ScoreAdsReactorTest,
       ReportingSuccessfulWithSellerAndBuyerCodeIsolationWithBuyerReportingId) {
  MockV8DispatchClient dispatcher;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};

  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  runtime_config.buyers_with_report_win_enabled.insert(
      winning_ad_score.interest_group_owner());
  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      winning_ad_score, kEuroIsoCode, kSellerDataVersion);
  post_auction_signals.highest_scoring_other_bid = 0;
  post_auction_signals.made_highest_scoring_other_bid = false;
  post_auction_signals.highest_scoring_other_bid_currency =
      kUnknownBidCurrencyCode;
  post_auction_signals.winning_ad_render_url = kTestAdRenderUrl;

  SellerReportingDispatchRequestData seller_dispatch_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);
  BuyerReportingDispatchRequestData buyer_dispatch_data =
      GetTestBuyerDispatchRequestData(log_context);
  EXPECT_EQ(buyer_dispatch_data.data_version, kTestDataVersion);
  EXPECT_EQ(seller_dispatch_data.post_auction_signals.seller_data_version,
            kSellerDataVersion);
  buyer_dispatch_data.made_highest_scoring_other_bid =
      post_auction_signals.made_highest_scoring_other_bid;
  AdWithBidMetadata foo;
  PopulateTestAdWithBidMetdata(post_auction_signals, buyer_dispatch_data, foo);
  EXPECT_EQ(foo.data_version(), kTestDataVersion);
  RawRequest raw_request = BuildRawRequest({foo});
  absl::StatusOr<std::string> seller_version = GetDefaultSellerUdfVersion();
  absl::StatusOr<std::string> buyer_version = GetBuyerReportWinVersion(
      foo.interest_group_owner(), AuctionType::kProtectedAudience);
  EXPECT_EQ(raw_request.ad_bids(0).data_version(), kTestDataVersion);

  {
    InSequence s;
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
          EXPECT_EQ(request.version_string, *seller_version);
          std::vector<absl::StatusOr<DispatchResponse>> responses;
          DispatchResponse response = {
              .id = request.id,
              .resp = absl::StrFormat(kTestScoreAdResponseTemplate,
                                      kTestDesirability)};
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, kReportResultEntryFunction);
          EXPECT_EQ(request.version_string, *seller_version);
          DispatchResponse response;
          response.resp = kTestReportResultResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&buyer_version, &seller_dispatch_data, &buyer_dispatch_data](
                      std::vector<DispatchRequest>& batch,
                      BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);

          const DispatchRequest& request = batch[0];
          VerifyReportWinInput(request, *buyer_version, seller_dispatch_data,
                               buyer_dispatch_data);
          DispatchResponse response;
          response.resp = kTestReportWinResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
  }

  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  VerifyReportingUrlsInScoreAdsResponse(raw_response);
}

TEST_F(ScoreAdsReactorTest,
       ReportingSuccessForComponentAuctionsWithSellerAndBuyerCodeIsolation) {
  MockV8DispatchClient dispatcher;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};

  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  runtime_config.buyers_with_report_win_enabled.insert(
      winning_ad_score.interest_group_owner());

  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      winning_ad_score, kEuroIsoCode, kSellerDataVersion);
  post_auction_signals.highest_scoring_other_bid = 0;
  post_auction_signals.made_highest_scoring_other_bid = false;
  post_auction_signals.highest_scoring_other_bid_currency =
      kUnknownBidCurrencyCode;
  post_auction_signals.winning_ad_render_url = kTestAdRenderUrl;

  SellerReportingDispatchRequestData seller_dispatch_data =
      GetTestSellerDispatchRequestDataForComponentAuction(post_auction_signals,
                                                          log_context);
  EXPECT_EQ(seller_dispatch_data.post_auction_signals.seller_data_version,
            kSellerDataVersion);
  BuyerReportingDispatchRequestData buyer_dispatch_data =
      GetTestBuyerDispatchRequestData(log_context);
  buyer_dispatch_data.made_highest_scoring_other_bid =
      post_auction_signals.made_highest_scoring_other_bid;
  AdWithBidMetadata bid_metadata;
  PopulateTestAdWithBidMetdata(post_auction_signals, buyer_dispatch_data,
                               bid_metadata);
  RawRequest raw_request = BuildRawRequest(
      {bid_metadata}, {.top_level_seller = kTestTopLevelSeller});
  absl::StatusOr<std::string> seller_version = GetDefaultSellerUdfVersion();
  absl::StatusOr<std::string> buyer_version = GetBuyerReportWinVersion(
      bid_metadata.interest_group_owner(), AuctionType::kProtectedAudience);

  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;
  {
    InSequence s;
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
          EXPECT_EQ(request.version_string, *seller_version);
          std::vector<absl::StatusOr<DispatchResponse>> responses;
          DispatchResponse response = {
              .id = request.id,
              .resp = absl::Substitute(kComponentWithCurrencyAdScoreTemplate,
                                       kTestDesirability,
                                       // NOLINTNEXTLINE
                                       /*bid=*/2.0, kEuroIsoCode,
                                       // NOLINTNEXTLINE
                                       /*incomingBidInSellerCurrency=*/1,
                                       // NOLINTNEXTLINE
                                       /*allowComponentAuction=*/"true")};
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, kReportResultEntryFunction);
          EXPECT_EQ(request.version_string, *seller_version);
          DispatchResponse response;
          response.resp = kTestReportResultResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&buyer_version, &seller_dispatch_data, &buyer_dispatch_data](
                      std::vector<DispatchRequest>& batch,
                      BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);

          const DispatchRequest& request = batch[0];
          VerifyReportWinInput(request, *buyer_version, seller_dispatch_data,
                               buyer_dispatch_data);
          DispatchResponse response;
          response.resp = kTestReportWinResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
  }

  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  VerifyReportingUrlsInScoreAdsResponseForComponentAuction(raw_response);
}

TEST_F(ScoreAdsReactorTest, NoBuyerReportingUrlReturnedWhenReportWinFails) {
  MockV8DispatchClient dispatcher;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};

  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  runtime_config.buyers_with_report_win_enabled.insert(
      winning_ad_score.interest_group_owner());

  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      winning_ad_score, kEuroIsoCode, kSellerDataVersion);
  post_auction_signals.highest_scoring_other_bid = 0;
  post_auction_signals.made_highest_scoring_other_bid = false;
  post_auction_signals.highest_scoring_other_bid_currency =
      kUnknownBidCurrencyCode;
  post_auction_signals.winning_ad_render_url = kTestAdRenderUrl;

  SellerReportingDispatchRequestData seller_dispatch_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);
  EXPECT_EQ(seller_dispatch_data.post_auction_signals.seller_data_version,
            kSellerDataVersion);
  BuyerReportingDispatchRequestData buyer_dispatch_data =
      GetTestBuyerDispatchRequestData(log_context);
  buyer_dispatch_data.made_highest_scoring_other_bid =
      post_auction_signals.made_highest_scoring_other_bid;
  AdWithBidMetadata foo;
  PopulateTestAdWithBidMetdata(post_auction_signals, buyer_dispatch_data, foo);
  RawRequest raw_request = BuildRawRequest({foo});
  absl::StatusOr<std::string> seller_version = GetDefaultSellerUdfVersion();
  absl::StatusOr<std::string> buyer_version = GetBuyerReportWinVersion(
      foo.interest_group_owner(), AuctionType::kProtectedAudience);

  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;
  {
    InSequence s;
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
          EXPECT_EQ(request.version_string, *seller_version);
          std::vector<absl::StatusOr<DispatchResponse>> responses;
          DispatchResponse response = {
              .id = request.id,
              .resp = absl::StrFormat(kTestScoreAdResponseTemplate,
                                      kTestDesirability)};
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, kReportResultEntryFunction);
          EXPECT_EQ(request.version_string, *seller_version);
          DispatchResponse response;
          response.resp = kTestReportResultResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&buyer_version, &seller_dispatch_data, &buyer_dispatch_data](
                      std::vector<DispatchRequest>& batch,
                      BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);

          const DispatchRequest& request = batch[0];
          VerifyReportWinInput(request, *buyer_version, seller_dispatch_data,
                               buyer_dispatch_data);
          DispatchResponse response;
          response.id = request.id;
          return absl::InternalError(kInternalServerError);
        });
  }

  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  VerifyOnlyReportResultUrlInScoreAdsResponse(raw_response);
}

TEST_F(ScoreAdsReactorTest,
       ReportingSuccessfulWithSellerAndBuyerCodeIsolationWithIgName) {
  MockV8DispatchClient dispatcher;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  AdWithBidMetadata foo = BuildTestAdWithBidMetadata();
  foo.set_bid_currency(kEurosIsoCode);
  foo.set_bid(1.0);
  runtime_config.buyers_with_report_win_enabled.insert(
      foo.interest_group_owner());
  RawRequest raw_request = BuildRawRequest({foo});

  absl::StatusOr<std::string> seller_version = GetDefaultSellerUdfVersion();
  absl::StatusOr<std::string> buyer_version = GetBuyerReportWinVersion(
      foo.interest_group_owner(), AuctionType::kProtectedAudience);
  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      winning_ad_score, kEuroIsoCode, kSellerDataVersion);
  post_auction_signals.highest_scoring_other_bid = 0;
  post_auction_signals.made_highest_scoring_other_bid = false;
  post_auction_signals.highest_scoring_other_bid_currency =
      kUnknownBidCurrencyCode;
  post_auction_signals.winning_ad_render_url = kTestAdRenderUrl;

  SellerReportingDispatchRequestData seller_dispatch_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);
  EXPECT_EQ(seller_dispatch_data.post_auction_signals.seller_data_version,
            kSellerDataVersion);
  BuyerReportingDispatchRequestData buyer_dispatch_data =
      GetTestBuyerDispatchRequestData(log_context);
  buyer_dispatch_data.buyer_reporting_id.reset();
  buyer_dispatch_data.made_highest_scoring_other_bid = false;
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;
  {
    InSequence s;
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
          EXPECT_EQ(request.version_string, *seller_version);
          std::vector<absl::StatusOr<DispatchResponse>> responses;
          DispatchResponse response = {
              .id = request.id,
              .resp = absl::StrFormat(kTestScoreAdResponseTemplate,
                                      kTestDesirability)};
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, kReportResultEntryFunction);
          EXPECT_EQ(request.version_string, *seller_version);
          DispatchResponse response;
          response.resp = kTestReportResultResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&buyer_version, &seller_dispatch_data, &buyer_dispatch_data](
                      std::vector<DispatchRequest>& batch,
                      BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);

          const DispatchRequest& request = batch[0];
          VerifyReportWinInput(request, *buyer_version, seller_dispatch_data,
                               buyer_dispatch_data);
          DispatchResponse response;
          response.resp = kTestReportWinResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
  }

  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  VerifyReportingUrlsInScoreAdsResponse(raw_response);
}

TEST_F(ScoreAdsReactorTest, RespectConfiguredDebugUrlLimits) {
  server_common::log::SetGlobalPSVLogLevel(10);
  MockV8DispatchClient dispatcher;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.enable_debug_reporting = true});
  std::string long_win_url(1024, 'A');
  std::string long_loss_url(1025, 'B');

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([long_win_url, long_loss_url](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        score_logic.reserve(batch.size());
        for (int i = 0; i < batch.size(); ++i) {
          score_logic.emplace_back(absl::Substitute(
              R"JSON(
                  {
                  "response" : {
                    "desirability" : $0,
                    "bid" : 1,
                    "allowComponentAuction" : false,
                    "debugReportUrls": {
                      "auctionDebugLossUrl": "$1",
                      "auctionDebugWinUrl": "$2"
                    }
                  },
                  "logs":[]
                  }
                )JSON",
              batch.size() - i, long_loss_url, long_win_url));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic));
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true,
      .max_allowed_size_all_debug_urls_kb = 1};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  PS_VLOG(5) << "Response:\n" << scored_ad.DebugString();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_TRUE(scored_ad.has_debug_report_urls());
  EXPECT_FALSE(scored_ad.debug_report_urls().auction_debug_win_url().empty());
  EXPECT_EQ(scored_ad.debug_report_urls().auction_debug_win_url().size(), 1024);
  EXPECT_TRUE(scored_ad.debug_report_urls().auction_debug_loss_url().empty());
}

ScoredAdData BuildScoredAdData(
    float desirability = 1.0, float buyer_bid = 1.0, bool k_anon_status = true,
    AdWithBidMetadata* protected_audience_ad_with_bid = nullptr,
    ProtectedAppSignalsAdWithBidMetadata* protected_app_signals_ad_with_bid =
        nullptr,
    absl::string_view id = "test_id") {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  absl::StatusOr<rapidjson::Document> response = ParseAndGetScoreAdResponseJson(
      /*enable_ad_tech_code_logging=*/true,
      absl::Substitute(
          R"JSON({
          "response" : {
            "desirability" : $0,
            "bid": $1
          },
          "logs": []
        })JSON",
          desirability, buyer_bid),
      /*log_context=*/log_context);
  CHECK_OK(response);
  ScoredAdData scored_ad_data = {
      .response_json = *std::move(response),
      .protected_audience_ad_with_bid = protected_audience_ad_with_bid,
      .protected_app_signals_ad_with_bid = protected_app_signals_ad_with_bid,
      .id = id,
      .k_anon_status = k_anon_status};
  auto& ad_score = scored_ad_data.ad_score;
  ad_score.set_desirability(desirability);
  ad_score.set_buyer_bid(buyer_bid);
  return scored_ad_data;
}

TEST_F(ScoreAdsReactorTest, ChoosesWinnerFromHighScoringAdsRandomly) {
  absl::SetFlag(&FLAGS_enable_kanon, false);
  MockV8DispatchClient dispatcher;
  // Create two ads that have highest (and equal) score and another one with
  // lower score.
  std::vector<float> scores = {kHighScore, kLowScore, kHighScore};
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata({kHighScoringRenderUrl1, kHighScoredBid,
                                  kHighScoringInterestGroupName1,
                                  kHighScoringInterestGroupOwner1,
                                  kHighScoringInterestGroupOrigin1}),
      BuildTestAdWithBidMetadata(
          {kLowScoringRenderUrl, kLowScoredBid, kLowScoringInterestGroupName,
           kLowScoringInterestGroupOwner, kLowScoringInterestGroupOrigin}),
      BuildTestAdWithBidMetadata({kHighScoringRenderUrl2, kHighScoredBid,
                                  kHighScoringInterestGroupName2,
                                  kHighScoringInterestGroupOwner2,
                                  kHighScoringInterestGroupOrigin2}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
  {
    "renderUrls": {
      "$0": [1],
      "$1": [2],
      "$2": [3]
    }
  })JSON",
      kHighScoringRenderUrl1, kLowScoringRenderUrl, kHighScoringRenderUrl2);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        absl::flat_hash_map<std::string, std::string> score_logic;
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic[ad_with_bid_metadata.render()] =
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid());
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  absl::flat_hash_map</*interest_group_owner*/ std::string, int>
      ig_owner_win_count;
  for (int i = 0; i < 50; ++i) {
    RawRequest raw_request = BuildRawRequest(
        ads_with_bid_metadata, {.scoring_signals = scoring_signals});
    raw_request.set_enforce_kanon(true);
    AuctionServiceRuntimeConfig runtime_config;
    auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
    ScoreAdsResponse::ScoreAdsRawResponse raw_response;
    ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
    const auto& ad_score = raw_response.ad_score();
    // Highest scored ad should win.
    ASSERT_EQ(ad_score.desirability(), kHighScore)
        << "Highest scored ad not selected as winner in " << i
        << "-th iteration";
    ig_owner_win_count[ad_score.interest_group_owner()] += 1;
  }
  ASSERT_EQ(ig_owner_win_count.size(), 2);
  for (const auto& [ig_owner, win_count] : ig_owner_win_count) {
    ASSERT_TRUE(ig_owner == kHighScoringInterestGroupOwner1 ||
                ig_owner == kHighScoringInterestGroupOwner2);
    EXPECT_GT(win_count, 0)
        << "Expected IG owner to win as well but it didn't: " << ig_owner;
  }
}

TEST_F(ScoreAdsReactorTest, AllAdsConsideredKAnonymousWhenKAnonNotEnforced) {
  absl::SetFlag(&FLAGS_enable_kanon, true);
  MockV8DispatchClient dispatcher;
  // Create an ad that is not k-anonymous.
  std::vector<float> scores = {kHighScore};
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata({.render_url = kHighScoringRenderUrl1,
                                  .bid = kHighScoredBid,
                                  .k_anon_status = false}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
      {
        "renderUrls": {
          "$0": [1]
        }
      })JSON",
      kHighScoringRenderUrl1);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic.push_back(
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid()));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  RawRequest raw_request = BuildRawRequest(
      ads_with_bid_metadata, {.scoring_signals = scoring_signals});
  raw_request.set_enforce_kanon(false);
  AuctionServiceRuntimeConfig runtime_config;
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& ad_score = raw_response.ad_score();
  // Highest scored ad should win since k-anon is not enforced.
  EXPECT_EQ(ad_score.desirability(), kHighScore);
}

TEST_F(ScoreAdsReactorTest, ChaffIfNoAdKAnonymousAndNoGhostWinners) {
  MockV8DispatchClient dispatcher;
  // Create an ad that is not k-anonymous.
  std::vector<float> scores = {kHighScore};
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata({.render_url = kHighScoringRenderUrl1,
                                  .bid = kHighScoredBid,
                                  .k_anon_status = false}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
      {
        "renderUrls": {
          "$0": [1]
        }
      })JSON",
      kHighScoringRenderUrl1);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic.push_back(
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid()));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  RawRequest raw_request = BuildRawRequest(
      ads_with_bid_metadata, {.scoring_signals = scoring_signals});
  raw_request.set_enforce_kanon(true);
  AuctionServiceRuntimeConfig runtime_config = {.enable_kanon = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  EXPECT_FALSE(raw_response.has_ad_score());
}

// Verifies that if there is a high scoring ad that is k-anonymous then we use
// it as a winner and there are no ghost winners.
TEST_F(ScoreAdsReactorTest, WinnerPrecedesGhostWinnerCandidates) {
  MockV8DispatchClient dispatcher;
  // Create an ad that is not k-anonymous.
  std::vector<float> scores = {kHighScore, kLowScore};
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata({.render_url = kHighScoringRenderUrl1,
                                  .bid = kHighScoredBid,
                                  .k_anon_status = true}),
      BuildTestAdWithBidMetadata({.render_url = kHighScoringRenderUrl2,
                                  .bid = kLowScoredBid,
                                  .k_anon_status = false}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
      {
        "renderUrls": {
          "$0": [1]
        }
      })JSON",
      kHighScoringRenderUrl1);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic.push_back(
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid()));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  RawRequest raw_request = BuildRawRequest(
      ads_with_bid_metadata, {.scoring_signals = scoring_signals});
  raw_request.set_enforce_kanon(true);
  AuctionServiceRuntimeConfig runtime_config = {.enable_kanon = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  ASSERT_TRUE(raw_response.has_ad_score());
  const auto& ad_score = raw_response.ad_score();
  EXPECT_EQ(ad_score.desirability(), kHighScore);
  EXPECT_EQ(raw_response.ghost_winning_ad_scores_size(), 0);
}

// Verifies that if there is a high scoring ad that is not k-anonymous then it
// is returned as a ghost winner and if there is a low scoring ad that is
// k-anonymous, it is used as the winner.
TEST_F(ScoreAdsReactorTest, GhostWinnerPresentIfTopScoringAdIsNotKAnonymous) {
  MockV8DispatchClient dispatcher;
  // Create: 1. A high scoring non-k anonymous ad which becomes a candidate for
  // ghost winner and 2. A low scoring k-anonymous ad which becomes the winnner.
  std::vector<float> scores = {kHighScore, kLowScore};
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata(
          {.render_url = kHighScoringRenderUrl1,
           .bid = kHighScoredBid,
           .interest_group_name = kHighScoringInterestGroupName1,
           .interest_group_owner = kHighScoringInterestGroupOwner1,
           .interest_group_origin = kHighScoringInterestGroupOrigin1,
           .k_anon_status = false}),
      BuildTestAdWithBidMetadata(
          {.render_url = kLowScoringRenderUrl,
           .bid = kLowScoredBid,
           .interest_group_name = kLowScoringInterestGroupName,
           .interest_group_owner = kLowScoringInterestGroupOwner,
           .interest_group_origin = kLowScoringInterestGroupOrigin,
           .k_anon_status = true}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
      {
        "renderUrls": {
          "$0": [1],
          "$1": [2]
        }
      })JSON",
      kHighScoringRenderUrl1, kLowScoringRenderUrl);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        absl::flat_hash_map<std::string, std::string> score_logic;
        score_logic.reserve(scores.size());
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic[ad_with_bid_metadata.render()] =
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid());
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  RawRequest raw_request = BuildRawRequest(
      ads_with_bid_metadata, {.scoring_signals = scoring_signals});
  raw_request.set_enforce_kanon(true);
  raw_request.set_num_allowed_ghost_winners(1);
  AuctionServiceRuntimeConfig runtime_config = {.enable_kanon = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  ASSERT_TRUE(raw_response.has_ad_score());
  const auto& ad_score = raw_response.ad_score();
  EXPECT_EQ(ad_score.desirability(), kLowScore);
  EXPECT_EQ(raw_response.ghost_winning_ad_scores_size(), 1);
}

// Verifies that if there are high scoring ads that are not k-anonymous then
// they are returned as ghost winner (even in complete absence of a real
// winner).
TEST_F(ScoreAdsReactorTest, GhostWinnersAreReturnedEvenIfthereIsNoWinner) {
  MockV8DispatchClient dispatcher;
  // Create two non-k anonymous ad which becomes a candidate for
  // ghost winner. Eventually we will choose the winner with the highest score.
  std::vector<float> scores = {kHighScore, kLowScore};
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata(
          {.render_url = kHighScoringRenderUrl1,
           .bid = kHighScoredBid,
           .interest_group_name = kHighScoringInterestGroupName1,
           .interest_group_owner = kHighScoringInterestGroupOwner1,
           .interest_group_origin = kHighScoringInterestGroupOrigin1,
           .k_anon_status = false}),
      BuildTestAdWithBidMetadata(
          {.render_url = kLowScoringRenderUrl,
           .bid = kLowScoredBid,
           .interest_group_name = kLowScoringInterestGroupName,
           .interest_group_owner = kLowScoringInterestGroupOwner,
           .interest_group_origin = kLowScoringInterestGroupOrigin,
           .k_anon_status = false}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
      {
        "renderUrls": {
          "$0": [1],
          "$1": [2]
        }
      })JSON",
      kHighScoringRenderUrl1, kLowScoringRenderUrl);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        absl::flat_hash_map<std::string, std::string> score_logic;
        score_logic.reserve(scores.size());
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic[ad_with_bid_metadata.render()] =
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid());
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  RawRequest raw_request = BuildRawRequest(
      ads_with_bid_metadata, {.scoring_signals = scoring_signals});
  raw_request.set_enforce_kanon(true);
  raw_request.set_num_allowed_ghost_winners(2);
  AuctionServiceRuntimeConfig runtime_config = {.enable_kanon = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  ASSERT_FALSE(raw_response.has_ad_score());
  ASSERT_EQ(raw_response.ghost_winning_ad_scores_size(), 1);
  // Only ghost winner with highest desirability is returned.
  EXPECT_EQ(raw_response.ghost_winning_ad_scores()[0].desirability(),
            kHighScore);
}

// Verifies that if there are multiple high scoring ads that are not k-anonymous
// then we choose ghost winners randomly from them.
TEST_F(ScoreAdsReactorTest, GhostWinnersAreChosenRandomly) {
  MockV8DispatchClient dispatcher;
  // Create 2 non-k-anonymous ads which will be candidate for ghost winners.
  // With all things equal in the ads (score, bid, k-anon status), expect the
  // ads to be chosen randomly as a ghost winner.
  std::vector<float> scores = {kHighScore, kHighScore};
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata(
          {.render_url = kHighScoringRenderUrl1,
           .bid = kHighScoredBid,
           .interest_group_name = kHighScoringInterestGroupName1,
           .interest_group_owner = kHighScoringInterestGroupOwner1,
           .interest_group_origin = kHighScoringInterestGroupOrigin1,
           .k_anon_status = false}),
      BuildTestAdWithBidMetadata(
          {.render_url = kLowScoringRenderUrl,
           .bid = kHighScoredBid,
           .interest_group_name = kLowScoringInterestGroupName,
           .interest_group_owner = kLowScoringInterestGroupOwner,
           .interest_group_origin = kLowScoringInterestGroupOrigin,
           .k_anon_status = false}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
      {
        "renderUrls": {
          "$0": [1],
          "$1": [2]
        }
      })JSON",
      kHighScoringRenderUrl1, kLowScoringRenderUrl);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        absl::flat_hash_map<std::string, std::string> score_logic;
        score_logic.reserve(scores.size());
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic[ad_with_bid_metadata.render()] =
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid());
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  absl::flat_hash_map<std::string, int> owner_count;
  for (int i = 0; i < 50; ++i) {
    RawRequest raw_request = BuildRawRequest(
        ads_with_bid_metadata, {.scoring_signals = scoring_signals});
    raw_request.set_enforce_kanon(true);
    raw_request.set_num_allowed_ghost_winners(1);
    AuctionServiceRuntimeConfig runtime_config = {.enable_kanon = true};
    auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
    ScoreAdsResponse::ScoreAdsRawResponse raw_response;
    ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
    ASSERT_FALSE(raw_response.has_ad_score());
    ASSERT_EQ(raw_response.ghost_winning_ad_scores_size(), 1);
    // Only ghost winner with highest desirability is returned.
    const auto& ghost_winner = raw_response.ghost_winning_ad_scores()[0];
    EXPECT_EQ(ghost_winner.desirability(), kHighScore);
    owner_count[ghost_winner.interest_group_owner()] += 1;
  }
  ASSERT_EQ(owner_count.size(), 2);
  for (const auto& [owner, count] : owner_count) {
    EXPECT_TRUE(owner == kHighScoringInterestGroupOwner1 ||
                owner == kLowScoringInterestGroupOwner);
    EXPECT_GT(count, 0);
  }
}

// Verifies that the upper limit of max ghost winners is respected.
TEST_F(ScoreAdsReactorTest, RespectsMaxGhostWinnersLimit) {
  MockV8DispatchClient dispatcher;
  std::vector<float> scores = {kHighScore, kLowScore};
  // Create: 1. A high scoring non-k anonymous ad which becomes a candidate for
  // ghost winner and 2. A low scoring k-anonymous ad which becomes the winnner.
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata(
          {.render_url = kHighScoringRenderUrl1,
           .bid = kHighScoredBid,
           .interest_group_name = kHighScoringInterestGroupName1,
           .interest_group_owner = kHighScoringInterestGroupOwner1,
           .interest_group_origin = kHighScoringInterestGroupOrigin1,
           .k_anon_status = false}),
      BuildTestAdWithBidMetadata(
          {.render_url = kLowScoringRenderUrl,
           .bid = kLowScoredBid,
           .interest_group_name = kLowScoringInterestGroupName,
           .interest_group_owner = kLowScoringInterestGroupOwner,
           .interest_group_origin = kLowScoringInterestGroupOrigin,
           .k_anon_status = true}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
      {
        "renderUrls": {
          "$0": [1],
          "$1": [2]
        }
      })JSON",
      kHighScoringRenderUrl1, kLowScoringRenderUrl);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        absl::flat_hash_map<std::string, std::string> score_logic;
        score_logic.reserve(scores.size());
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic[ad_with_bid_metadata.render()] =
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid());
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  RawRequest raw_request = BuildRawRequest(
      ads_with_bid_metadata, {.scoring_signals = scoring_signals});
  raw_request.set_enforce_kanon(true);
  // Set a limit of zero so that the ghost winner is trimmed out due to this
  // limit.
  raw_request.set_num_allowed_ghost_winners(0);
  AuctionServiceRuntimeConfig runtime_config = {.enable_kanon = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  ASSERT_TRUE(raw_response.has_ad_score());
  const auto& ad_score = raw_response.ad_score();
  EXPECT_EQ(ad_score.desirability(), kLowScore);
  EXPECT_EQ(raw_response.ghost_winning_ad_scores_size(), 0);
}

// Verifies that the highes_scoring_other_bid never contains ghost winning ad.
TEST_F(ScoreAdsReactorTest, GhostWinnerNotCandidatesForHighestScoringOtherBid) {
  MockV8DispatchClient dispatcher;
  std::vector<float> scores = {kHighScore, kLowScore,
                               kHighestScoringOtherBidsScore};
  ASSERT_GE(kLowScoredBid, kHighestScoringOtherBidsScore)
      << "Highest scoring other bid score should be lesser than equal to the "
         "winner's score";
  // Creates 3 ads. The highest scoring ad will be used as ghost winner since
  // it is not k-anonymous, the second highest scoring ad will be the winner
  // and the lowest scored ad will become the highest scoring other bid.
  std::vector<AdWithBidMetadata> ads_with_bid_metadata = {
      BuildTestAdWithBidMetadata(
          {.render_url = kHighScoringRenderUrl1,
           .bid = kHighScoredBid,
           .interest_group_name = kHighScoringInterestGroupName1,
           .interest_group_owner = kHighScoringInterestGroupOwner1,
           .interest_group_origin = kHighScoringInterestGroupOrigin1,
           .k_anon_status = false}),
      BuildTestAdWithBidMetadata(
          {.render_url = kLowScoringRenderUrl,
           .bid = kLowScoredBid,
           .interest_group_name = kLowScoringInterestGroupName,
           .interest_group_owner = kLowScoringInterestGroupOwner,
           .interest_group_origin = kLowScoringInterestGroupOrigin,
           .k_anon_status = true}),
      // k-anon status doesn't matter for HSOB.
      BuildTestAdWithBidMetadata(
          {.render_url = kHighestScoringOtherBidRenderUrl,
           .bid = kHighestScoringOtherBidsBidValue,
           .interest_group_name = kHighestScoringOtherBidInterestGroupName,
           .interest_group_owner = kLowScoringInterestGroupOwner,
           .interest_group_origin = kHighestScoringOtherBidInterestGroupOrigin,
           .k_anon_status = false}),
  };
  std::string scoring_signals = absl::Substitute(
      R"JSON(
      {
        "renderUrls": {
          "$0": [1],
          "$1": [2],
          "$2": [3]
        }
      })JSON",
      kHighScoringRenderUrl1, kLowScoringRenderUrl,
      kHighestScoringOtherBidRenderUrl);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([scores, ads_with_bid_metadata](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        absl::flat_hash_map<std::string, std::string> score_logic;
        score_logic.reserve(scores.size());
        for (int i = 0; i < scores.size(); ++i) {
          const AdWithBidMetadata& ad_with_bid_metadata =
              ads_with_bid_metadata[i];
          score_logic[ad_with_bid_metadata.render()] =
              absl::Substitute(R"({
              "response" : {
                "desirability" : $0,
                "render": "$1",
                "interest_group_name": "$2",
                "interest_group_owner": "$3",
                "interest_group_origin": "$4",
                "bid" : $5
              },
              "logs":[]})",
                               scores[i], ad_with_bid_metadata.render(),
                               ad_with_bid_metadata.interest_group_name(),
                               ad_with_bid_metadata.interest_group_owner(),
                               ad_with_bid_metadata.interest_group_origin(),
                               ad_with_bid_metadata.bid());
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  RawRequest raw_request = BuildRawRequest(
      ads_with_bid_metadata, {.scoring_signals = scoring_signals});
  raw_request.set_enforce_kanon(true);
  raw_request.set_num_allowed_ghost_winners(1);
  AuctionServiceRuntimeConfig runtime_config = {.enable_kanon = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  ASSERT_TRUE(raw_response.has_ad_score());
  const auto& ad_score = raw_response.ad_score();
  EXPECT_EQ(ad_score.desirability(), kLowScore);
  const auto& ig_owner_highest_scoring_other_bids_map =
      ad_score.ig_owner_highest_scoring_other_bids_map();
  ASSERT_EQ(ig_owner_highest_scoring_other_bids_map.size(), 1);
  auto it = ig_owner_highest_scoring_other_bids_map.find(
      kLowScoringInterestGroupOwner);
  ASSERT_NE(it, ig_owner_highest_scoring_other_bids_map.end());
  const auto& highest_scoring_bid_values = it->second.values();
  ASSERT_EQ(highest_scoring_bid_values.size(), 1);
  EXPECT_EQ(highest_scoring_bid_values[0].number_value(),
            kHighestScoringOtherBidsBidValue);

  ASSERT_EQ(raw_response.ghost_winning_ad_scores_size(), 1);
  const auto& ghost_winner = raw_response.ghost_winning_ad_scores()[0];
  EXPECT_EQ(ghost_winner.desirability(), kHighScore);
}

class ScoredAdDataTest : public ::testing::Test {
 protected:
  AdWithBidMetadata protected_audience_ad_with_bid_1_;
  ProtectedAppSignalsAdWithBidMetadata protected_app_signals_ad_with_bid_1_;
  AdWithBidMetadata protected_audience_ad_with_bid_2_;
  ProtectedAppSignalsAdWithBidMetadata protected_app_signals_ad_with_bid_2_;
};

TEST_F(ScoredAdDataTest, SwapsAllData) {
  ScoredAdData scored_ad_data_1 = BuildScoredAdData(
      kTestDesirability1, kTestBuyerBid1, kTestAnonStatusFalse,
      &protected_audience_ad_with_bid_1_, &protected_app_signals_ad_with_bid_1_,
      kTestScoredAdDataId1);
  ScoredAdData scored_ad_data_2 = BuildScoredAdData(
      kTestDesirability2, kTestBuyerBid2, kTestAnonStatusTrue,
      &protected_audience_ad_with_bid_2_, &protected_app_signals_ad_with_bid_2_,
      kTestScoredAdDataId2);

  // Keep tabs on the previous state before swap happens.
  ScoreAdsResponse::AdScore prev_ad_score_1 = scored_ad_data_1.ad_score;
  ScoreAdsResponse::AdScore prev_ad_score_2 = scored_ad_data_2.ad_score;

  // Keep track of previous response JSONs.
  rapidjson::Document prev_response_json_1;
  prev_response_json_1.CopyFrom(scored_ad_data_1.response_json,
                                prev_response_json_1.GetAllocator());
  rapidjson::Document prev_response_json_2;
  prev_response_json_2.CopyFrom(scored_ad_data_2.response_json,
                                prev_response_json_2.GetAllocator());

  // Swap now and test the data is swapped.
  scored_ad_data_2.Swap(scored_ad_data_1);
  EXPECT_THAT(scored_ad_data_1.ad_score, EqualsProto(prev_ad_score_2));
  EXPECT_THAT(scored_ad_data_2.ad_score, EqualsProto(prev_ad_score_1));
  EXPECT_EQ(scored_ad_data_1.ad_score.desirability(), kTestDesirability2);
  EXPECT_EQ(scored_ad_data_2.ad_score.desirability(), kTestDesirability1);
  EXPECT_EQ(scored_ad_data_1.protected_audience_ad_with_bid,
            &protected_audience_ad_with_bid_2_);
  EXPECT_EQ(scored_ad_data_2.protected_audience_ad_with_bid,
            &protected_audience_ad_with_bid_1_);
  EXPECT_EQ(scored_ad_data_1.protected_app_signals_ad_with_bid,
            &protected_app_signals_ad_with_bid_2_);
  EXPECT_EQ(scored_ad_data_2.protected_app_signals_ad_with_bid,
            &protected_app_signals_ad_with_bid_1_);
  EXPECT_EQ(scored_ad_data_1.id, kTestScoredAdDataId2);
  EXPECT_EQ(scored_ad_data_2.id, kTestScoredAdDataId1);
  EXPECT_EQ(scored_ad_data_1.k_anon_status, kTestAnonStatusTrue);
  EXPECT_EQ(scored_ad_data_2.k_anon_status, kTestAnonStatusFalse);
  EXPECT_EQ(scored_ad_data_1.response_json, prev_response_json_2);
  EXPECT_EQ(scored_ad_data_2.response_json, prev_response_json_1);
}

TEST_F(ScoredAdDataTest, ScoredAdDataWithHighScoreIsConsideredBigger) {
  ScoredAdData ad_with_high_score_less_bid = BuildScoredAdData(
      kTestDesirability1, kTestBuyerBid1, kTestAnonStatusFalse,
      &protected_audience_ad_with_bid_1_, &protected_app_signals_ad_with_bid_1_,
      kTestScoredAdDataId1);

  ScoredAdData ad_with_low_score_high_bid = BuildScoredAdData(
      kTestDesirability1 - 1.0, kTestBuyerBid1 + 10, kTestAnonStatusTrue,
      &protected_audience_ad_with_bid_2_, &protected_app_signals_ad_with_bid_2_,
      kTestScoredAdDataId2);

  EXPECT_GT(ad_with_high_score_less_bid, ad_with_low_score_high_bid);
}

TEST_F(ScoredAdDataTest, ScoredAdDataWithHighBidIsConsideredBigger) {
  ScoredAdData ad_with_same_score_less_bid = BuildScoredAdData(
      kTestDesirability1, kTestBuyerBid1, kTestAnonStatusTrue,
      &protected_audience_ad_with_bid_1_, &protected_app_signals_ad_with_bid_1_,
      kTestScoredAdDataId1);

  ScoredAdData ad_with_same_score_high_bid = BuildScoredAdData(
      kTestDesirability1, kTestBuyerBid1 + 10, kTestAnonStatusFalse,
      &protected_audience_ad_with_bid_2_, &protected_app_signals_ad_with_bid_2_,
      kTestScoredAdDataId2);

  EXPECT_GT(ad_with_same_score_high_bid, ad_with_same_score_less_bid);
}

TEST_F(ScoredAdDataTest, ScoredAdDataMeetingKAnonThresholdIsBigger) {
  ScoredAdData ad_with_same_score_bid_k_anon = BuildScoredAdData(
      kTestDesirability1, kTestBuyerBid1, kTestAnonStatusTrue,
      &protected_audience_ad_with_bid_1_, &protected_app_signals_ad_with_bid_1_,
      kTestScoredAdDataId1);

  ScoredAdData ad_with_same_score_bid_non_k_anon = BuildScoredAdData(
      kTestDesirability1, kTestBuyerBid1, kTestAnonStatusFalse,
      &protected_audience_ad_with_bid_2_, &protected_app_signals_ad_with_bid_2_,
      kTestScoredAdDataId2);

  EXPECT_GT(ad_with_same_score_bid_k_anon, ad_with_same_score_bid_non_k_anon);
}

TEST_F(ScoredAdDataTest, ScoredAdDataWithSameScoreBidAndKAnonAreEqual) {
  ScoredAdData ad_with_same_score_bid_k_anon_1 = BuildScoredAdData(
      kTestDesirability1, kTestBuyerBid1, kTestAnonStatusTrue,
      &protected_audience_ad_with_bid_1_, &protected_app_signals_ad_with_bid_1_,
      kTestScoredAdDataId1);

  ScoredAdData ad_with_same_score_bid_k_anon_2 = BuildScoredAdData(
      kTestDesirability1, kTestBuyerBid1, kTestAnonStatusTrue,
      &protected_audience_ad_with_bid_2_, &protected_app_signals_ad_with_bid_2_,
      kTestScoredAdDataId2);

  EXPECT_FALSE(ad_with_same_score_bid_k_anon_1 >
               ad_with_same_score_bid_k_anon_2);
  EXPECT_FALSE(ad_with_same_score_bid_k_anon_2 >
               ad_with_same_score_bid_k_anon_1);
}

TEST_F(ScoreAdsReactorTest,
       ReportingSuccessWithCodeIsolationWithBuyerAndSellerReportingId) {
  MockV8DispatchClient dispatcher;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  runtime_config.buyers_with_report_win_enabled.insert(
      winning_ad_score.interest_group_owner());
  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      winning_ad_score, kEuroIsoCode, kSellerDataVersion);
  post_auction_signals.highest_scoring_other_bid = 0;
  post_auction_signals.made_highest_scoring_other_bid = false;
  post_auction_signals.highest_scoring_other_bid_currency =
      kUnknownBidCurrencyCode;
  post_auction_signals.winning_ad_render_url = kTestAdRenderUrl;

  SellerReportingDispatchRequestData seller_dispatch_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);
  EXPECT_EQ(seller_dispatch_data.post_auction_signals.seller_data_version,
            kSellerDataVersion);
  BuyerReportingDispatchRequestData buyer_dispatch_data =
      GetTestBuyerDispatchRequestData(log_context);
  buyer_dispatch_data.buyer_reporting_id = "";
  buyer_dispatch_data.buyer_and_seller_reporting_id =
      kBuyerAndSellerReportingId;
  buyer_dispatch_data.made_highest_scoring_other_bid = false;
  AdWithBidMetadata ad;
  PopulateTestAdWithBidMetdata(post_auction_signals, buyer_dispatch_data, ad);
  ad.set_bid_currency(kEurosIsoCode);
  ad.set_bid(1.0);
  RawRequest raw_request = BuildRawRequest({ad});
  absl::StatusOr<std::string> seller_version = GetDefaultSellerUdfVersion();
  absl::StatusOr<std::string> buyer_version = GetBuyerReportWinVersion(
      ad.interest_group_owner(), AuctionType::kProtectedAudience);
  {
    InSequence s;
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
          EXPECT_EQ(request.version_string, *seller_version);
          std::vector<absl::StatusOr<DispatchResponse>> responses;
          DispatchResponse response = {
              .id = request.id,
              .resp = absl::StrFormat(kTestScoreAdResponseTemplate,
                                      kTestDesirability)};
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&seller_version](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          EXPECT_EQ(request.handler_name, kReportResultEntryFunction);
          EXPECT_EQ(request.version_string, *seller_version);
          absl::StatusOr<rapidjson::Document> seller_signals =
              ParseJsonString(*request.input[ReportResultArgIndex(
                  ReportResultArgs::kSellerReportingSignals)]);
          std::string buyer_and_seller_id;
          PS_ASSIGN_IF_PRESENT(buyer_and_seller_id, seller_signals.value(),
                               kBuyerAndSellerReportingIdTag, String);
          EXPECT_TRUE(buyer_and_seller_id.empty());
          DispatchResponse response;
          response.resp = kTestReportResultResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([&buyer_version, &seller_dispatch_data, &buyer_dispatch_data](
                      std::vector<DispatchRequest>& batch,
                      BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);

          const DispatchRequest& request = batch[0];
          absl::StatusOr<rapidjson::Document> buyer_signals =
              ParseJsonString(*request.input[PAReportWinArgIndex(
                  PAReportWinArgs::kBuyerReportingSignals)]);
          std::string buyer_and_seller_id;
          PS_ASSIGN_IF_PRESENT(buyer_and_seller_id, buyer_signals.value(),
                               kBuyerAndSellerReportingIdTag, String);
          EXPECT_EQ(buyer_and_seller_id, kBuyerAndSellerReportingId);
          VerifyReportWinInput(request, *buyer_version, seller_dispatch_data,
                               buyer_dispatch_data);
          DispatchResponse response;
          response.resp = kTestReportWinResponseJson;
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
  }

  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  VerifyReportingUrlsInScoreAdsResponse(raw_response);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
