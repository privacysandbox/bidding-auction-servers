// Copyright 2024 Google LLC
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

#include "services/seller_frontend_service/util/proto_mapping_util.h"

#include <gmock/gmock.h>

#include <string>

#include "absl/container/btree_map.h"
#include "api/bidding_auction_servers_cc_proto_builder.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/seller_frontend_service/data/k_anon.h"
#include "services/seller_frontend_service/test/constants.h"
#include "services/seller_frontend_service/test/kanon_test_utils.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/core/test/utils/proto_test_utils.h"
#include "src/encryption/key_fetcher/fake_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using google::scp::core::test::EqualsProto;

inline constexpr char kEmptyAdAuctionResultNonce[] = "";
inline constexpr char kRandomAdAuctionResultNonce[] = "ad-auction-n0nce";
inline constexpr char kTestSeller[] = "sample-seller";
inline constexpr char kBuyer1[] = "bg1";
inline constexpr char kBuyer2[] = "bg2";
inline constexpr char kTestOwner[] = "test-owner";
inline constexpr char kTestRender[] = "test-render";
inline constexpr char kTestComponentRender[] = "test-component-render";
inline constexpr char kTestBuyerReportingId[] = "buyer-reporting-id";
inline constexpr char kTestBuyerAndSellerReportingId[] =
    "buyer-seller-reporting-id";

ScoreAdsResponse::AdScore MakeAnAdScore() {
  // Populate rejection reasons and highest other buyers.
  auto score = MakeARandomAdScore(
      /*hob_buyer_entries = */ 2,
      /*rejection_reason_ig_owners = */ 2,
      /*rejection_reason_ig_per_owner = */ 2);
  return score;
}

AuctionResult::Error MakeAnError() {
  AuctionResult::Error error;
  error.set_code(MakeARandomInt(0, 10));
  error.set_message(MakeARandomString());
  return error;
}

PrivateAggregateContribution CreateTestPAggContribution(
    EventType event_type, absl::string_view event_name = "") {
  // Create the example SignalValue
  SignalValue signal_value;
  signal_value.set_base_value(BASE_VALUE_WINNING_BID);  // Set the base_value
  signal_value.set_offset(10);                          // Set some offset value
  signal_value.set_scale(1.5);                          // Set scale factor

  // Create the example SignalBucket
  SignalBucket signal_bucket;
  signal_bucket.set_base_value(
      BASE_VALUE_HIGHEST_SCORING_OTHER_BID);  // Set the base_value
  signal_bucket.set_scale(2.0);               // Set scale factor

  // Create the BucketOffset
  BucketOffset bucket_offset;
  bucket_offset.add_value(12345678901234567890ULL);  // Add 64-bit values
  bucket_offset.add_value(12345678901234567890ULL);  // Add another 64-bit value
  bucket_offset.set_is_negative(true);               // Set the is_negative flag

  // Set the BucketOffset in SignalBucket
  *signal_bucket.mutable_offset() = bucket_offset;

  // Create the PrivateAggregationValue with SignalValue
  PrivateAggregationValue private_aggregation_value;
  *private_aggregation_value.mutable_extended_value() = signal_value;

  // Create the PrivateAggregationBucket with SignalBucket
  PrivateAggregationBucket private_aggregation_bucket;
  *private_aggregation_bucket.mutable_signal_bucket() = signal_bucket;

  // Create a PrivateAggregateContribution and set its fields
  PrivateAggregateContribution contribution;
  *contribution.mutable_bucket() = private_aggregation_bucket;
  *contribution.mutable_value() = private_aggregation_value;
  contribution.mutable_event()->set_event_type(event_type);
  contribution.mutable_event()->set_event_name(event_name);
  return contribution;
}

AuctionResult MapBasicScoreFieldsToAuctionResult(
    const ScoreAdsResponse::AdScore& high_score) {
  AuctionResult auction_result;
  auction_result.set_is_chaff(false);
  if (high_score.bid() > 0) {
    auction_result.set_bid(high_score.bid());
  }
  auction_result.set_score(high_score.desirability());
  auction_result.set_interest_group_name(high_score.interest_group_name());
  auction_result.set_interest_group_owner(high_score.interest_group_owner());
  auction_result.set_interest_group_origin(high_score.interest_group_origin());
  auction_result.set_ad_render_url(high_score.render());
  auction_result.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(high_score.win_reporting_urls()
                              .buyer_reporting_urls()
                              .reporting_url());
  for (auto& [event, url] : high_score.win_reporting_urls()
                                .buyer_reporting_urls()
                                .interaction_reporting_urls()) {
    auction_result.mutable_win_reporting_urls()
        ->mutable_buyer_reporting_urls()
        ->mutable_interaction_reporting_urls()
        ->try_emplace(event, url);
  }
  auction_result.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(high_score.win_reporting_urls()
                              .top_level_seller_reporting_urls()
                              .reporting_url());
  for (auto& [event, url] : high_score.win_reporting_urls()
                                .top_level_seller_reporting_urls()
                                .interaction_reporting_urls()) {
    auction_result.mutable_win_reporting_urls()
        ->mutable_top_level_seller_reporting_urls()
        ->mutable_interaction_reporting_urls()
        ->try_emplace(event, url);
  }

  auction_result.mutable_win_reporting_urls()
      ->mutable_component_seller_reporting_urls()
      ->set_reporting_url(high_score.win_reporting_urls()
                              .component_seller_reporting_urls()
                              .reporting_url());
  for (auto& [event, url] : high_score.win_reporting_urls()
                                .component_seller_reporting_urls()
                                .interaction_reporting_urls()) {
    auction_result.mutable_win_reporting_urls()
        ->mutable_component_seller_reporting_urls()
        ->mutable_interaction_reporting_urls()
        ->try_emplace(event, url);
  }
  *auction_result.mutable_ad_component_render_urls() =
      high_score.component_renders();
  auction_result.set_ad_type(high_score.ad_type());
  return auction_result;
}

void MapBasicScoreFieldsToAuctionResultForPrivateAggregation(
    ScoreAdsResponse::AdScore& high_score, AuctionResult& auction_result) {
  PrivateAggregateContribution contribution =
      CreateTestPAggContribution(EVENT_TYPE_WIN);
  PrivateAggregateReportingResponse pagg_response;
  *pagg_response.add_contributions() = contribution;
  *auction_result.add_top_level_contributions() = pagg_response;
  *high_score.add_top_level_contributions() = std::move(pagg_response);
}

AuctionResult MapBasicErrorFieldsToAuctionResult(
    const AuctionResult::Error& error) {
  AuctionResult auction_result;
  *auction_result.mutable_error() = error;
  return auction_result;
}

void MapServerComponentAuctionFieldsToAuctionResult(
    AuctionResult* auction_result, absl::string_view seller,
    absl::string_view generation_id) {
  ASSERT_GT(generation_id.length(), 0);
  ASSERT_GT(seller.length(), 0);
  auction_result->mutable_auction_params()->set_ciphertext_generation_id(
      generation_id);
  auction_result->mutable_auction_params()->set_component_seller(seller);
}

void MapServerComponentAuctionFieldsToAuctionResult(
    AuctionResult* auction_result, absl::string_view seller,
    const ProtectedAuctionInput& protected_auction_input) {
  MapServerComponentAuctionFieldsToAuctionResult(
      auction_result, seller, protected_auction_input.generation_id());
}

void MapServerComponentAuctionFieldsToAuctionResult(
    AuctionResult* auction_result, absl::string_view seller,
    const ProtectedAudienceInput& protected_audience_input) {
  MapServerComponentAuctionFieldsToAuctionResult(
      auction_result, seller, protected_audience_input.generation_id());
}

class AdScoreToAuctionResultTest : public testing::Test {
 public:
  void SetUp() override {
    valid_score_ = MakeAnAdScore();
    valid_seller_ = MakeARandomString();
    valid_error_ = MakeAnError();
    valid_audience_input_.set_generation_id(MakeARandomString());
    valid_auction_input_ =
        MakeARandomProtectedAuctionInput(ClientType::CLIENT_TYPE_ANDROID);
  }

 protected:
  std::string empty_seller_;
  ProtectedAudienceInput empty_audience_;
  std::string valid_seller_;
  ProtectedAudienceInput valid_audience_input_;
  ProtectedAuctionInput valid_auction_input_;
  ScoreAdsResponse::AdScore valid_score_;
  AuctionResult::Error valid_error_;
};

TEST_F(AdScoreToAuctionResultTest, MapsAllFieldsForSingleSellerAuction) {
  UpdateGroupMap empty_updates;
  AuctionResult expected = MapBasicScoreFieldsToAuctionResult(valid_score_);
  AuctionResult output = AdScoreToAuctionResult(
      valid_score_, /*maybe_bidding_groups=*/std::nullopt, empty_updates,
      /*error=*/std::nullopt, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER,
      empty_seller_, empty_audience_);
  EXPECT_THAT(expected, EqualsProto(output));
}

TEST_F(AdScoreToAuctionResultTest,
       MapsAllFieldsForSingleSellerAuctionWithPAAPIContributions) {
  UpdateGroupMap empty_updates;
  AuctionResult expected = MapBasicScoreFieldsToAuctionResult(valid_score_);
  MapBasicScoreFieldsToAuctionResultForPrivateAggregation(valid_score_,
                                                          expected);
  AuctionResult output = AdScoreToAuctionResult(
      valid_score_, /*maybe_bidding_groups=*/std::nullopt, empty_updates,
      /*error=*/std::nullopt, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER,
      empty_seller_, empty_audience_);
  EXPECT_THAT(expected, EqualsProto(output));
}

TEST_F(AdScoreToAuctionResultTest, MapsAllFieldsForTopLevelAuction) {
  UpdateGroupMap empty_updates;
  AuctionResult expected = MapBasicScoreFieldsToAuctionResult(valid_score_);
  AuctionResult output = AdScoreToAuctionResult(
      valid_score_, /*maybe_bidding_groups=*/std::nullopt, empty_updates,
      /*error=*/std::nullopt,
      AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER, empty_seller_,
      empty_audience_);
  EXPECT_THAT(expected, EqualsProto(output));
}

TEST_F(AdScoreToAuctionResultTest, MapsAllFieldsForDeviceComponentAuction) {
  UpdateGroupMap empty_updates;
  AuctionResult expected = MapBasicScoreFieldsToAuctionResult(valid_score_);
  AuctionResult output = AdScoreToAuctionResult(
      valid_score_, /*maybe_bidding_groups=*/std::nullopt, empty_updates,
      /*error=*/std::nullopt,
      AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER, empty_seller_,
      empty_audience_);
  EXPECT_THAT(expected, EqualsProto(output));
}

TEST_F(AdScoreToAuctionResultTest,
       MapsAllFieldsForServerAuctionForProtectedAuctionInput) {
  UpdateGroupMap empty_updates;
  AuctionResult expected = MapBasicScoreFieldsToAuctionResult(valid_score_);
  MapServerComponentAuctionFieldsToAuctionResult(&expected, valid_seller_,
                                                 valid_auction_input_);
  AuctionResult output = AdScoreToAuctionResult(
      valid_score_, /*maybe_bidding_groups=*/std::nullopt, empty_updates,
      /*error=*/std::nullopt,
      AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER, valid_seller_,
      valid_auction_input_);
  EXPECT_THAT(expected, EqualsProto(output));
}

TEST_F(AdScoreToAuctionResultTest,
       MapsAllFieldsForServerAuctionForProtectedAudienceInput) {
  UpdateGroupMap empty_updates;
  AuctionResult expected = MapBasicScoreFieldsToAuctionResult(valid_score_);
  MapServerComponentAuctionFieldsToAuctionResult(&expected, valid_seller_,
                                                 valid_audience_input_);
  AuctionResult output = AdScoreToAuctionResult(
      valid_score_, /*maybe_bidding_groups=*/std::nullopt, empty_updates,
      /*error=*/std::nullopt,
      AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER, valid_seller_,
      valid_audience_input_);
  EXPECT_THAT(expected, EqualsProto(output));
}

TEST_F(AdScoreToAuctionResultTest, MapsErrorOverAdScoreIfPresent) {
  UpdateGroupMap empty_updates;
  AuctionResult expected = MapBasicErrorFieldsToAuctionResult(valid_error_);
  AuctionResult output = AdScoreToAuctionResult(
      std::nullopt, /*maybe_bidding_groups=*/std::nullopt, empty_updates,
      valid_error_, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER, empty_seller_,
      empty_audience_);
  EXPECT_THAT(expected, EqualsProto(output));
}

ScoreAdsResponse::AdScore SampleAdScore() {
  ScoreAdsResponse::AdScore ad_score;
  ad_score.set_desirability(kSampleScore);
  ad_score.set_render(kSampleAdRenderUrl);
  ad_score.set_interest_group_name(kSampleIgName);
  ad_score.set_buyer_bid(kSampleBid);
  ad_score.set_interest_group_owner(kSampleIgOwner);
  ad_score.set_interest_group_origin(kSampleIgOrigin);
  ad_score.set_ad_metadata(kSampleAdMetadata);
  ad_score.set_allow_component_auction(false);
  ad_score.set_bid(kSampleBid);
  return ad_score;
}

class AdScoreToAuctionResultWithKAnonTest : public AdScoreToAuctionResultTest {
 public:
  void SetUp() {
    AdScoreToAuctionResultTest::SetUp();
    valid_score_ = SampleAdScore();
    kanon_auction_result_data_ = SampleKAnonAuctionResultData(
        {.ig_index = kSampleIgIndex,
         .ig_owner = kSampleIgOwner,
         .ig_name = kSampleIgName,
         .bucket_name =
             std::vector<uint8_t>(kSampleBucket.begin(), kSampleBucket.end()),
         .bucket_value = kSampleValue,
         .ad_render_url = kSampleAdRenderUrl,
         .ad_component_render_url = kSampleAdComponentRenderUrl,
         .modified_bid = kSampleModifiedBid,
         .bid_currency = kSampleBidCurrency,
         .ad_metadata = kSampleAdMetadata,
         .buyer_reporting_id = kSampleBuyerReportingId,
         .buyer_and_seller_reporting_id = kSampleBuyerAndSellerReportingId,
         .selected_buyer_and_seller_reporting_id =
             kSampleSelectedBuyerAndSellerReportingId,
         .ad_render_url_hash = std::vector<uint8_t>(
             kSampleAdRenderUrlHash.begin(), kSampleAdRenderUrlHash.end()),
         .ad_component_render_urls_hash =
             std::vector<uint8_t>(kSampleAdComponentRenderUrlsHash.begin(),
                                  kSampleAdComponentRenderUrlsHash.end()),
         .reporting_id_hash = std::vector<uint8_t>(
             kSampleReportingIdHash.begin(), kSampleReportingIdHash.end()),
         .winner_positional_index = kSampleWinnerPositionalIndex});
  }

 protected:
  std::unique_ptr<KAnonAuctionResultData> kanon_auction_result_data_;
};

TEST_F(AdScoreToAuctionResultWithKAnonTest, MapsKAnonData) {
  UpdateGroupMap empty_updates;
  AuctionResult expected;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_render_url: "adRenderUrl"
        interest_group_name: "foo_ig_name"
        interest_group_owner: "foo_owner"
        score: 1.12
        bid: 1.75
        win_reporting_urls {
          buyer_reporting_urls {}
          component_seller_reporting_urls {}
          top_level_seller_reporting_urls {}
        }
        interest_group_origin: "ig_origin"
        k_anon_winner_join_candidates {
          ad_render_url_hash: "\001\002\003\004\005"
          ad_component_render_urls_hash: "\005\004\003\002\001"
          reporting_id_hash: "\002\004\006\010\t"
        }
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\001\002\003\004\005"
            ad_component_render_urls_hash: "\005\004\003\002\001"
            reporting_id_hash: "\002\004\006\010\t"
          }
          interest_group_index: 10
          owner: "foo_owner"
          ig_name: "foo_ig_name"
          ghost_winner_private_aggregation_signals {
            bucket: "\001\003\005\007\t"
            value: 21
          }
          ghost_winner_for_top_level_auction {
            ad_render_url: "adRenderUrl"
            ad_component_render_urls: "adComponentRenderUrl"
            modified_bid: 1.23
            bid_currency: "bidCurrency"
            ad_metadata: "adMetadata"
            buyer_reporting_id: "buyerReportingId"
            selected_buyer_and_seller_reporting_id: "selectedBuyerAndSellerReportingId"
            buyer_and_seller_reporting_id: "buyerAndSellerReportingId"
          }
        }
      )pb",
      &expected));
  AuctionResult output = AdScoreToAuctionResult(
      valid_score_, /*maybe_bidding_groups=*/std::nullopt, empty_updates,
      /*error=*/std::nullopt, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER,
      empty_seller_, empty_audience_, /*top_level_seller=*/"",
      std::move(kanon_auction_result_data_));
  EXPECT_THAT(output, EqualsProto(expected));
}

ScoreAdsRequest::ScoreAdsRawRequest MapScoreAdsRawRequest(
    const SelectAdRequest::AuctionConfig& auction_config,
    std::variant<ProtectedAudienceInput, ProtectedAuctionInput>&
        protected_auction_input,
    std::vector<AuctionResult>& component_auction_results) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  *raw_request.mutable_auction_signals() = auction_config.auction_signals();
  *raw_request.mutable_seller_signals() = auction_config.seller_signals();
  *raw_request.mutable_seller() = auction_config.seller();
  *raw_request.mutable_seller_currency() = auction_config.seller_currency();

  raw_request.mutable_component_auction_results()->Assign(
      component_auction_results.begin(), component_auction_results.end());
  std::visit(
      [&raw_request, &auction_config](const auto& protected_auction_input) {
        raw_request.set_publisher_hostname(
            protected_auction_input.publisher_name());
        raw_request.set_enable_debug_reporting(
            protected_auction_input.enable_debug_reporting());
        auto* log_context = raw_request.mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(auction_config.seller_debug_id());
        if (protected_auction_input.has_consented_debug_config()) {
          *raw_request.mutable_consented_debug_config() =
              protected_auction_input.consented_debug_config();
        }
      },
      protected_auction_input);
  return raw_request;
}

template <typename T>
class CreateScoreAdsRawRequestTest : public ::testing::Test {
 protected:
  void SetUp() override {
    for (int i = 0; i < 10; i++) {
      component_auctions_list_.emplace_back(
          MakeARandomSingleSellerAuctionResult());
    }
    auto [protected_auction_input, request, context] =
        GetSampleSelectAdRequest<T>(CLIENT_TYPE_BROWSER, kTestSeller,
                                    /*is_consented_debug=*/true);
    protected_auction_input_ = {protected_auction_input};
    request_ = request;
    expected_ = MapScoreAdsRawRequest(request_.auction_config(),
                                      protected_auction_input_,
                                      component_auctions_list_);
  }

 public:
  std::vector<AuctionResult> component_auctions_list_;
  ScoreAdsRequest::ScoreAdsRawRequest expected_;
  std::variant<ProtectedAudienceInput, ProtectedAuctionInput>
      protected_auction_input_;
  SelectAdRequest request_;
};

using ProtectedAuctionInputTypes =
    ::testing::Types<ProtectedAudienceInput, ProtectedAuctionInput>;
TYPED_TEST_SUITE(CreateScoreAdsRawRequestTest, ProtectedAuctionInputTypes);

TYPED_TEST(CreateScoreAdsRawRequestTest, MapsAllFields) {
  MapScoreAdsRawRequest(this->request_.auction_config(),
                        this->protected_auction_input_,
                        this->component_auctions_list_);
  auto output = CreateTopLevelScoreAdsRawRequest(
      this->request_.auction_config(), this->protected_auction_input_,
      this->component_auctions_list_);
  EXPECT_THAT(this->expected_, EqualsProto(*output));
}

TYPED_TEST(CreateScoreAdsRawRequestTest, IgnoresChaffResults) {
  AuctionResult chaff_res;
  chaff_res.set_is_chaff(true);
  std::vector<AuctionResult> list_with_chaff = this->component_auctions_list_;
  list_with_chaff.push_back(chaff_res);
  MapScoreAdsRawRequest(this->request_.auction_config(),
                        this->protected_auction_input_,
                        this->component_auctions_list_);
  auto output = CreateTopLevelScoreAdsRawRequest(
      this->request_.auction_config(), this->protected_auction_input_,
      list_with_chaff);
  EXPECT_THAT(this->expected_, EqualsProto(*output));
}

TEST(GetBuyerIgsWithBidsMapTest, CollatesMapsFromAllAuctionResults) {
  std::vector<IgsWithBidsMap> input;
  input.emplace_back(MakeARandomComponentAuctionResult(
                         MakeARandomString(), MakeARandomString(), {kBuyer1})
                         .bidding_groups());
  input.emplace_back(MakeARandomComponentAuctionResult(
                         MakeARandomString(), MakeARandomString(), {kBuyer2})
                         .bidding_groups());

  // copy to expected
  std::vector<IgsWithBidsMap> expected = input;
  IgsWithBidsMap output = GetBuyerIgsWithBidsMap(input);
  EXPECT_EQ(output.size(), 2);
  ASSERT_TRUE(output.find(kBuyer1) != output.end());
  EXPECT_THAT(output.at(kBuyer1), EqualsProto(expected[0].at(kBuyer1)));
  ASSERT_TRUE(output.find(kBuyer2) != output.end());
  EXPECT_THAT(output.at(kBuyer2), EqualsProto(expected[1].at(kBuyer2)));
}

TEST(GetBuyerIgsWithBidsMapTest, MergesKeysAcrossAuctionResults) {
  std::vector<IgsWithBidsMap> input;
  input.emplace_back(MakeARandomComponentAuctionResult(
                         MakeARandomString(), MakeARandomString(), {kBuyer1})
                         .bidding_groups());
  input.emplace_back(MakeARandomComponentAuctionResult(
                         MakeARandomString(), MakeARandomString(), {kBuyer1})
                         .bidding_groups());

  absl::flat_hash_set<int> ig_idx_set;
  ig_idx_set.insert(input[0].at(kBuyer1).index().begin(),
                    input[0].at(kBuyer1).index().end());
  ig_idx_set.insert(input[1].at(kBuyer1).index().begin(),
                    input[1].at(kBuyer1).index().end());

  AuctionResult::InterestGroupIndex expected_ig_idx;
  expected_ig_idx.mutable_index()->Assign(ig_idx_set.begin(), ig_idx_set.end());

  IgsWithBidsMap output = GetBuyerIgsWithBidsMap(input);
  EXPECT_EQ(output.size(), 1);
  ASSERT_TRUE(output.find(kBuyer1) != output.end());
  EXPECT_THAT(output.at(kBuyer1), EqualsProto(expected_ig_idx));
}

class UnpackageServerAuctionComponentResultTest : public ::testing::Test {
 protected:
  void SetUp() {
    crypto_client_ = CreateCryptoClient();
    key_fetcher_manager_ =
        std::make_unique<server_common::FakeKeyFetcherManager>();
  }
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<CryptoClientWrapperInterface> crypto_client_;
};

TEST_F(UnpackageServerAuctionComponentResultTest, UnpacksAuctionResult) {
  AuctionResult expected = MakeARandomSingleSellerAuctionResult();
  std::string plaintext_response =
      FrameAndCompressProto(expected.SerializeAsString());
  auto encrypted_request =
      HpkeEncrypt(plaintext_response, *crypto_client_, *key_fetcher_manager_,
                  server_common::CloudPlatform::kGcp);
  ASSERT_TRUE(encrypted_request.ok()) << encrypted_request.status();
  SelectAdRequest::ComponentAuctionResult input;
  *input.mutable_key_id() = std::move(encrypted_request.value().key_id);
  *input.mutable_auction_result_ciphertext() =
      std::move(encrypted_request.value().ciphertext);

  auto output = UnpackageServerAuctionComponentResult(input, *crypto_client_,
                                                      *key_fetcher_manager_);
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(*output, EqualsProto(expected));
}

TEST_F(UnpackageServerAuctionComponentResultTest, ReturnsErrorForInvalidData) {
  SelectAdRequest::ComponentAuctionResult input;
  // Tink Crashes if payload is < 32 chars.
  *input.mutable_auction_result_ciphertext() =
      absl::StrCat(MakeARandomString(), MakeARandomString());
  *input.mutable_key_id() = MakeARandomString();
  auto output = UnpackageServerAuctionComponentResult(input, *crypto_client_,
                                                      *key_fetcher_manager_);
  EXPECT_FALSE(output.ok());
}

class CreateAuctionResultCiphertextTest : public testing::Test {
  void SetUp() override {
    valid_score_ = MakeAnAdScore();
    valid_error_ = MakeAnError();
    AuctionResult random_result = MakeARandomSingleSellerAuctionResult();
    bidding_group_map_ = random_result.bidding_groups();
    update_group_map_ = random_result.update_groups();
    absl::btree_map<std::string, std::string> context_map;
    log_context_ = std::make_unique<RequestLogContext>(
        context_map, server_common::ConsentedDebugConfiguration());
  }

 protected:
  ScoreAdsResponse::AdScore valid_score_;
  AuctionResult::Error valid_error_;
  IgsWithBidsMap bidding_group_map_;
  UpdateGroupMap update_group_map_;
  std::unique_ptr<RequestLogContext> log_context_;
  std::unique_ptr<OhttpHpkeDecryptedMessage> MakeDecryptedMessage() {
    auto [request, context] =
        GetFramedInputAndOhttpContext(MakeARandomString());
    auto private_key = GetPrivateKey();
    return std::make_unique<OhttpHpkeDecryptedMessage>(
        MakeARandomString(), context, private_key,
        kBiddingAuctionOhttpRequestLabel);
  }
};

TEST_F(CreateAuctionResultCiphertextTest, ConvertsTopLevelAdScoreForAndroid) {
  UpdateGroupMap empty_updates;
  AuctionResult expected =
      MapBasicScoreFieldsToAuctionResult(this->valid_score_);
  ScoreAdsResponse::AdScore top_level_score = this->valid_score_;
  top_level_score.set_seller(MakeARandomString());
  expected.mutable_auction_params()->set_component_seller(
      top_level_score.seller());
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateNonChaffAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, top_level_score, std::nullopt, empty_updates,
      ClientType::CLIENT_TYPE_ANDROID, *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(DecryptAppProtoAuctionResult(*output, decrypted_message->context),
              EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsAdScoreForWeb) {
  AuctionResult expected =
      MapBasicScoreFieldsToAuctionResult(this->valid_score_);
  // For web, the bid is encoded from the buyer_bid field
  expected.set_bid(this->valid_score_.buyer_bid());
  // IG Origin is Android exclusive and will not be parsed or encoded for web.
  expected.clear_interest_group_origin();
  expected.mutable_bidding_groups()->insert(this->bidding_group_map_.begin(),
                                            this->bidding_group_map_.end());
  expected.mutable_update_groups()->insert(this->update_group_map_.begin(),
                                           this->update_group_map_.end());
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateNonChaffAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, this->valid_score_, this->bidding_group_map_,
      this->update_group_map_, ClientType::CLIENT_TYPE_BROWSER,
      *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(
      DecryptBrowserAuctionResultAndNonce(*output, decrypted_message->context)
          .first,
      EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsAdScoreForWebWithNonce) {
  AuctionResult expected =
      MapBasicScoreFieldsToAuctionResult(this->valid_score_);
  // For web, the bid is encoded from the buyer_bid field
  expected.set_bid(this->valid_score_.buyer_bid());
  // IG Origin is Android exclusive and will not be parsed or encoded for web.
  expected.clear_interest_group_origin();
  expected.mutable_bidding_groups()->insert(this->bidding_group_map_.begin(),
                                            this->bidding_group_map_.end());
  expected.mutable_update_groups()->insert(this->update_group_map_.begin(),
                                           this->update_group_map_.end());
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateNonChaffAuctionResultCiphertext(
      kRandomAdAuctionResultNonce, this->valid_score_, this->bidding_group_map_,
      this->update_group_map_, ClientType::CLIENT_TYPE_BROWSER,
      *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();

  auto actual =
      DecryptBrowserAuctionResultAndNonce(*output, decrypted_message->context);
  EXPECT_THAT(actual.first, EqualsProto(expected));
  EXPECT_EQ(actual.second, kRandomAdAuctionResultNonce);
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsAdScoreForWebWithKAnonData) {
  auto decrypted_message = MakeDecryptedMessage();
  auto k_anon_auction_result_data = SampleKAnonAuctionResultData(
      {.ig_index = kSampleIgIndex,
       .ig_owner = kSampleIgOwner,
       .ig_name = kSampleIgName,
       .bucket_name =
           std::vector<uint8_t>(kSampleBucket.begin(), kSampleBucket.end()),
       .bucket_value = kSampleValue,
       .ad_render_url = kSampleAdRenderUrl,
       .ad_component_render_url = kSampleAdComponentRenderUrl,
       .modified_bid = kSampleModifiedBid,
       .bid_currency = kSampleBidCurrency,
       .ad_metadata = kSampleAdMetadata,
       .buyer_reporting_id = kSampleBuyerReportingId,
       .buyer_and_seller_reporting_id = kSampleBuyerAndSellerReportingId,
       .selected_buyer_and_seller_reporting_id =
           kSampleSelectedBuyerAndSellerReportingId,
       .ad_render_url_hash = std::vector<uint8_t>(
           kSampleAdRenderUrlHash.begin(), kSampleAdRenderUrlHash.end()),
       .ad_component_render_urls_hash =
           std::vector<uint8_t>(kSampleAdComponentRenderUrlsHash.begin(),
                                kSampleAdComponentRenderUrlsHash.end()),
       .reporting_id_hash = std::vector<uint8_t>(kSampleReportingIdHash.begin(),
                                                 kSampleReportingIdHash.end()),
       .winner_positional_index = kSampleWinnerPositionalIndex});
  // We clean the input data to CreateNonChaffAuctionResultCiphertext to
  // avoid confusion about this method populating fields for top level auction.
  // In real usage, the caller of the method is responsible for ensuring that
  // the ghost winners for top level auctions are not present in the input.
  for (auto& ghost_winner : *k_anon_auction_result_data->kanon_ghost_winners) {
    ghost_winner.clear_ghost_winner_for_top_level_auction();
  }
  ScoreAdsResponse::AdScore ad_score =
      ScoreAdsResponse_AdScoreBuilder()
          .SetInterestGroupOwner(kTestOwner)
          .SetRender(kTestRender)
          .AddComponentRenders(kTestComponentRender)
          .SetBuyerReportingId(kTestBuyerReportingId)
          .SetBuyerAndSellerReportingId(kTestBuyerAndSellerReportingId);
  auto output = CreateNonChaffAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, ad_score, {}, {},
      ClientType::CLIENT_TYPE_BROWSER, *decrypted_message, *this->log_context_,
      std::move(k_anon_auction_result_data));
  ASSERT_TRUE(output.ok()) << output.status();

  AuctionResult expected;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_render_url: "test-render"
        ad_component_render_urls: "test-component-render"
        interest_group_owner: "test-owner"
        buyer_reporting_id: "buyer-reporting-id"
        k_anon_winner_join_candidates {
          ad_render_url_hash: "\001\002\003\004\005"
          ad_component_render_urls_hash: "\005\004\003\002\001"
          reporting_id_hash: "\002\004\006\010\t"
        }
        k_anon_winner_positional_index: 5
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\001\002\003\004\005"
            ad_component_render_urls_hash: "\005\004\003\002\001"
            reporting_id_hash: "\002\004\006\010\t"
          }
          interest_group_index: 10
          owner: "foo_owner"
          ig_name: "foo_ig_name"
          ghost_winner_private_aggregation_signals {
            bucket: "\001\003\005\007\t"
            value: 21
          }
        }
      )pb",
      &expected));
  EXPECT_THAT(
      DecryptBrowserAuctionResultAndNonce(*output, decrypted_message->context)
          .first,
      EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest, ReturnsErrorForInvalidClientAdScore) {
  UpdateGroupMap empty_updates;
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateNonChaffAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, this->valid_score_, this->bidding_group_map_,
      empty_updates, ClientType::CLIENT_TYPE_UNKNOWN, *decrypted_message,
      *this->log_context_);

  ASSERT_FALSE(output.ok());
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsClientErrorForAndroid) {
  AuctionResult expected =
      MapBasicErrorFieldsToAuctionResult(this->valid_error_);
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateErrorAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, this->valid_error_,
      ClientType::CLIENT_TYPE_ANDROID, *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(DecryptAppProtoAuctionResult(*output, decrypted_message->context),
              EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsClientErrorForWeb) {
  AuctionResult expected =
      MapBasicErrorFieldsToAuctionResult(this->valid_error_);
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateErrorAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, this->valid_error_,
      ClientType::CLIENT_TYPE_BROWSER, *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(
      DecryptBrowserAuctionResultAndNonce(*output, decrypted_message->context)
          .first,
      EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsClientErrorForWebWithNonce) {
  AuctionResult expected =
      MapBasicErrorFieldsToAuctionResult(this->valid_error_);
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateErrorAuctionResultCiphertext(
      kRandomAdAuctionResultNonce, this->valid_error_,
      ClientType::CLIENT_TYPE_BROWSER, *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  auto actual =
      DecryptBrowserAuctionResultAndNonce(*output, decrypted_message->context);
  EXPECT_THAT(actual.first, EqualsProto(expected));
  EXPECT_EQ(actual.second, kRandomAdAuctionResultNonce);
}

TEST_F(CreateAuctionResultCiphertextTest,
       ReturnsErrorForInvalidClientErrorMessage) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateErrorAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, this->valid_error_,
      ClientType::CLIENT_TYPE_UNKNOWN, *decrypted_message, *this->log_context_);

  ASSERT_FALSE(output.ok());
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsChaffForAndroid) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateChaffAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, ClientType::CLIENT_TYPE_ANDROID,
      *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  AuctionResult decrypted_output =
      DecryptAppProtoAuctionResult(*output, decrypted_message->context);
  EXPECT_TRUE(decrypted_output.is_chaff());
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsChaffForWeb) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateChaffAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, ClientType::CLIENT_TYPE_BROWSER,
      *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  AuctionResult decrypted_output =
      DecryptBrowserAuctionResultAndNonce(*output, decrypted_message->context)
          .first;
  EXPECT_TRUE(decrypted_output.is_chaff());
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsChaffForWebWithNonce) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateChaffAuctionResultCiphertext(
      kRandomAdAuctionResultNonce, ClientType::CLIENT_TYPE_BROWSER,
      *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  auto decrypted_output =
      DecryptBrowserAuctionResultAndNonce(*output, decrypted_message->context);
  EXPECT_TRUE(decrypted_output.first.is_chaff());
  EXPECT_EQ(decrypted_output.second, kRandomAdAuctionResultNonce);
}

TEST_F(CreateAuctionResultCiphertextTest, ReturnsErrorForInvalidClientChaff) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateChaffAuctionResultCiphertext(
      kEmptyAdAuctionResultNonce, ClientType::CLIENT_TYPE_UNKNOWN,
      *decrypted_message, *this->log_context_);

  ASSERT_FALSE(output.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
