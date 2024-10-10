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
#include "gtest/gtest.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/core/test/utils/proto_test_utils.h"
#include "src/encryption/key_fetcher/fake_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using google::scp::core::test::EqualsProto;

constexpr char kTestSeller[] = "sample-seller";
constexpr char kBuyer1[] = "bg1";
constexpr char kBuyer2[] = "bg2";

ScoreAdsResponse::AdScore MakeAnAdScore() {
  // Populate rejection reasons and highest other buyers.
  return MakeARandomAdScore(
      /*hob_buyer_entries = */ 2,
      /*rejection_reason_ig_owners = */ 2,
      /*rejection_reason_ig_per_owner = */ 2);
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

TEST_F(CreateAuctionResultCiphertextTest, ConvertsAdScoreForAndroid) {
  UpdateGroupMap empty_updates;
  AuctionResult expected =
      MapBasicScoreFieldsToAuctionResult(this->valid_score_);
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateWinningAuctionResultCiphertext(
      this->valid_score_, std::nullopt, empty_updates,
      ClientType::CLIENT_TYPE_ANDROID, *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(DecryptAppProtoAuctionResult(*output, decrypted_message->context),
              EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsAdScoreForWeb) {
  AuctionResult expected =
      MapBasicScoreFieldsToAuctionResult(this->valid_score_);
  // IG Origin is Android exclusive and will not be parsed or encoded for web.
  expected.clear_interest_group_origin();
  expected.mutable_bidding_groups()->insert(this->bidding_group_map_.begin(),
                                            this->bidding_group_map_.end());
  expected.mutable_update_groups()->insert(this->update_group_map_.begin(),
                                           this->update_group_map_.end());
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateWinningAuctionResultCiphertext(
      this->valid_score_, this->bidding_group_map_, this->update_group_map_,
      ClientType::CLIENT_TYPE_BROWSER, *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(DecryptBrowserAuctionResult(*output, decrypted_message->context),
              EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest, ReturnsErrorForInvalidClientAdScore) {
  UpdateGroupMap empty_updates;
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateWinningAuctionResultCiphertext(
      this->valid_score_, this->bidding_group_map_, empty_updates,
      ClientType::CLIENT_TYPE_UNKNOWN, *decrypted_message, *this->log_context_);

  ASSERT_FALSE(output.ok());
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsClientErrorForAndroid) {
  AuctionResult expected =
      MapBasicErrorFieldsToAuctionResult(this->valid_error_);
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateErrorAuctionResultCiphertext(
      this->valid_error_, ClientType::CLIENT_TYPE_ANDROID, *decrypted_message,
      *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(DecryptAppProtoAuctionResult(*output, decrypted_message->context),
              EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsClientErrorForWeb) {
  AuctionResult expected =
      MapBasicErrorFieldsToAuctionResult(this->valid_error_);
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateErrorAuctionResultCiphertext(
      this->valid_error_, ClientType::CLIENT_TYPE_BROWSER, *decrypted_message,
      *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_THAT(DecryptBrowserAuctionResult(*output, decrypted_message->context),
              EqualsProto(expected));
}

TEST_F(CreateAuctionResultCiphertextTest,
       ReturnsErrorForInvalidClientErrorMessage) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateErrorAuctionResultCiphertext(
      this->valid_error_, ClientType::CLIENT_TYPE_UNKNOWN, *decrypted_message,
      *this->log_context_);

  ASSERT_FALSE(output.ok());
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsChaffForAndroid) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateChaffAuctionResultCiphertext(
      ClientType::CLIENT_TYPE_ANDROID, *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  AuctionResult decrypted_output =
      DecryptAppProtoAuctionResult(*output, decrypted_message->context);
  EXPECT_TRUE(decrypted_output.is_chaff());
}

TEST_F(CreateAuctionResultCiphertextTest, ConvertsChaffForWeb) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateChaffAuctionResultCiphertext(
      ClientType::CLIENT_TYPE_BROWSER, *decrypted_message, *this->log_context_);

  ASSERT_TRUE(output.ok()) << output.status();
  AuctionResult decrypted_output =
      DecryptBrowserAuctionResult(*output, decrypted_message->context);
  EXPECT_TRUE(decrypted_output.is_chaff());
}

TEST_F(CreateAuctionResultCiphertextTest, ReturnsErrorForInvalidClientChaff) {
  auto decrypted_message = MakeDecryptedMessage();
  auto output = CreateChaffAuctionResultCiphertext(
      ClientType::CLIENT_TYPE_UNKNOWN, *decrypted_message, *this->log_context_);

  ASSERT_FALSE(output.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
