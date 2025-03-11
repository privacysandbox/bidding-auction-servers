
/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "services/seller_frontend_service/k_anon/k_anon_utils.h"

#include <vector>

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers_cc_proto_builder.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "include/gmock/gmock.h"
#include "services/common/private_aggregation/private_aggregation_test_util.h"
#include "services/seller_frontend_service/private_aggregation/private_aggregation_helper.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr char kTestOwner[] = "test-owner";
inline constexpr char kTestGhostOwner[] = "test-ghost-owner";
inline constexpr char kTestGhostIgName[] = "test-ghost-ig-name";
inline constexpr char kTestRender[] = "test-render";
inline constexpr char kTestGhostRender[] = "test-ghost-render";
inline constexpr char kTestReportWinUrl[] = "test-report-win-url";
inline constexpr char kTestGhostReportWinUrl[] = "test-ghost-report-win-url";
inline constexpr char kTestComponentRender[] = "test-component-render";
inline constexpr char kTestGhostComponentRender[] =
    "test-ghost-component-render";
inline constexpr char kTestBuyerReportingId[] = "buyer-reporting-id";
inline constexpr char kTestGhostBuyerReportingId[] = "ghost-buyer-reporting-id";
inline constexpr char kTestBuyerAndSellerReportingId[] =
    "buyer-seller-reporting-id";
inline constexpr char kTestGhostBuyerAndSellerReportingId[] =
    "ghost-buyer-seller-reporting-id";
using ::google::scp::core::test::EqualsProto;
using AdScores =
    ::google::protobuf::RepeatedPtrField<ScoreAdsResponse::AdScore>;
TEST(KAnonUtilsTest, CreatesKAnonJoinCandidate) {
  ScoreAdsResponse::AdScore ad_score =
      ScoreAdsResponse_AdScoreBuilder()
          .SetInterestGroupOwner(kTestOwner)
          .SetRender(kTestRender)
          .AddComponentRenders(kTestComponentRender)
          .SetBuyerReportingId(kTestBuyerReportingId)
          .SetBuyerAndSellerReportingId(kTestBuyerAndSellerReportingId);
  KAnonJoinCandidate expected;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_render_url_hash: "fi\312Z\303\362\334\036\237aO\310\310\354OD#\313\215H\023\375u<\227*\323\333<\213\373*"
        ad_component_render_urls_hash: "j\272`G\214<\256\240\000@\333p\252\311e\030R\265\360%Se\252\200\010\004\ni\237\017\253!"
        reporting_id_hash: "\253\206\273\264\2467\353+(\335\270\204>\206\375P=\037v\326\206\331\314w\235N{Z\353\216\211\020"
      )pb",
      &expected));
  EXPECT_THAT(
      GetKAnonJoinCandidate(
          ad_score,
          {.buyer_report_win_js_urls = {{kTestOwner, kTestReportWinUrl}}}),
      EqualsProto(expected));
}
TEST(KAnonUtilsTest, CreatesKAnonAuctionResultDataForSingleSellerAuctions) {
  ScoreAdsResponse::AdScore winning_ad_score =
      ScoreAdsResponse_AdScoreBuilder()
          .SetInterestGroupOwner(kTestOwner)
          .SetRender(kTestRender)
          .AddComponentRenders(kTestComponentRender)
          .SetBuyerReportingId(kTestBuyerReportingId)
          .SetBuyerAndSellerReportingId(kTestBuyerAndSellerReportingId);
  std::vector<PrivateAggregateContribution> test_contribution = {
      GetTestContributionWithIntegers(EventType::EVENT_TYPE_LOSS,
                                      /* event_name= */ "")};
  const PrivateAggregateReportingResponse response =
      GetTestPrivateAggregateResponse(test_contribution, kTestGhostOwner);
  AdScores ghost_winner_ad_scores;
  *ghost_winner_ad_scores.Add() =
      ScoreAdsResponse_AdScoreBuilder()
          .SetInterestGroupOwner(kTestGhostOwner)
          .SetInterestGroupName(kTestGhostIgName)
          .SetRender(kTestGhostRender)
          .AddComponentRenders(kTestGhostComponentRender)
          .SetBuyerReportingId(kTestGhostBuyerReportingId)
          .SetBuyerAndSellerReportingId(kTestGhostBuyerAndSellerReportingId)
          .AddTopLevelContributions(response);
  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ReportWinMap report_win_map = {
      .buyer_report_win_js_urls = {{kTestOwner, kTestReportWinUrl},
                                   {kTestGhostOwner, kTestGhostReportWinUrl}}};
  KAnonAuctionResultData k_anon_auction_result_data = GetKAnonAuctionResultData(
      winning_ad_score, &ghost_winner_ad_scores,
      /*is_component_auction=*/false,
      [&report_win_map](const ScoreAdsResponse::AdScore& ad_score) {
        return GetKAnonJoinCandidate(ad_score, report_win_map);
      },
      log_context);
  KAnonJoinCandidate expected_winner;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_render_url_hash: "fi\312Z\303\362\334\036\237aO\310\310\354OD#\313\215H\023\375u<\227*\323\333<\213\373*"
        ad_component_render_urls_hash: "j\272`G\214<\256\240\000@\333p\252\311e\030R\265\360%Se\252\200\010\004\ni\237\017\253!"
        reporting_id_hash: "\253\206\273\264\2467\353+(\335\270\204>\206\375P=\037v\326\206\331\314w\235N{Z\353\216\211\020"
      )pb",
      &expected_winner));
  ASSERT_NE(k_anon_auction_result_data.kanon_winner_join_candidates, nullptr);
  EXPECT_THAT(*k_anon_auction_result_data.kanon_winner_join_candidates,
              EqualsProto(expected_winner));
  AuctionResult::KAnonGhostWinner expected_k_anon_ghost_winner;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        k_anon_join_candidates {
          ad_render_url_hash: "fi\312Z\303\362\334\036\237aO\310\310\354OD#\313\215H\023\375u<\227*\323\333<\213\373*"
          ad_component_render_urls_hash: "j\272`G\214<\256\240\000@\333p\252\311e\030R\265\360%Se\252\200\010\004\ni\237\017\253!"
          reporting_id_hash: "d\315\363c\034\210\203\211\202\206\223^\247\031`\024z\215\267\227\243\262\222\261F\325\204\02538m\206"
        }
        owner: "test-ghost-owner"
        ig_name: "test-ghost-ig-name"
        ghost_winner_private_aggregation_signals {
          bucket: "\001#Eg\211\253\315\357\0224Vx\232\274\336\360"
          value: 10
        }
      )pb",
      &expected_k_anon_ghost_winner));
  ASSERT_NE(k_anon_auction_result_data.kanon_ghost_winners, nullptr);
  ASSERT_EQ(k_anon_auction_result_data.kanon_ghost_winners->size(), 1);
  EXPECT_THAT(k_anon_auction_result_data.kanon_ghost_winners->at(0),
              EqualsProto(expected_k_anon_ghost_winner));
  ASSERT_GT(k_anon_auction_result_data.kanon_ghost_winners->at(0)
                .mutable_ghost_winner_private_aggregation_signals()
                ->size(),
            0);
  EXPECT_EQ(k_anon_auction_result_data.kanon_ghost_winners->at(0)
                .ghost_winner_private_aggregation_signals()[0]
                .bucket(),
            ConvertIntArrayToByteString(response.contributions()[0].bucket()));
  EXPECT_EQ(k_anon_auction_result_data.kanon_ghost_winners->at(0)
                .ghost_winner_private_aggregation_signals()[0]
                .value(),
            test_contribution[0].value().int_value());
}
TEST(KAnonUtilsTest, CreatesKAnonAuctionResultDataForComponentAuctions) {
  ScoreAdsResponse::AdScore winning_ad_score =
      ScoreAdsResponse_AdScoreBuilder()
          .SetInterestGroupOwner(kTestOwner)
          .SetRender(kTestRender)
          .AddComponentRenders(kTestComponentRender)
          .SetBuyerReportingId(kTestBuyerReportingId)
          .SetBuyerAndSellerReportingId(kTestBuyerAndSellerReportingId);
  AdScores ghost_winner_ad_scores;
  *ghost_winner_ad_scores.Add() =
      ScoreAdsResponse_AdScoreBuilder()
          .SetInterestGroupOwner(kTestGhostOwner)
          .SetInterestGroupName(kTestGhostIgName)
          .SetRender(kTestGhostRender)
          .AddComponentRenders(kTestGhostComponentRender)
          .SetBuyerReportingId(kTestGhostBuyerReportingId)
          .SetBuyerAndSellerReportingId(kTestGhostBuyerAndSellerReportingId);
  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ReportWinMap report_win_map = {
      .buyer_report_win_js_urls = {{kTestOwner, kTestReportWinUrl},
                                   {kTestGhostOwner, kTestGhostReportWinUrl}}};
  KAnonAuctionResultData k_anon_auction_result_data = GetKAnonAuctionResultData(
      winning_ad_score, &ghost_winner_ad_scores,
      /*is_component_auction=*/true,
      [&report_win_map](const ScoreAdsResponse::AdScore& ad_score) {
        return GetKAnonJoinCandidate(ad_score, report_win_map);
      },
      log_context);
  KAnonJoinCandidate expected_winner;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_render_url_hash: "fi\312Z\303\362\334\036\237aO\310\310\354OD#\313\215H\023\375u<\227*\323\333<\213\373*"
        ad_component_render_urls_hash: "j\272`G\214<\256\240\000@\333p\252\311e\030R\265\360%Se\252\200\010\004\ni\237\017\253!"
        reporting_id_hash: "\253\206\273\264\2467\353+(\335\270\204>\206\375P=\037v\326\206\331\314w\235N{Z\353\216\211\020"
      )pb",
      &expected_winner));
  ASSERT_NE(k_anon_auction_result_data.kanon_winner_join_candidates, nullptr);
  EXPECT_THAT(*k_anon_auction_result_data.kanon_winner_join_candidates,
              EqualsProto(expected_winner));
  AuctionResult::KAnonGhostWinner expected_k_anon_ghost_winner;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        k_anon_join_candidates {
          ad_render_url_hash: "fi\312Z\303\362\334\036\237aO\310\310\354OD#\313\215H\023\375u<\227*\323\333<\213\373*"
          ad_component_render_urls_hash: "j\272`G\214<\256\240\000@\333p\252\311e\030R\265\360%Se\252\200\010\004\ni\237\017\253!"
          reporting_id_hash: "d\315\363c\034\210\203\211\202\206\223^\247\031`\024z\215\267\227\243\262\222\261F\325\204\02538m\206"
        }
        owner: "test-ghost-owner"
        ig_name: "test-ghost-ig-name"
        ghost_winner_for_top_level_auction {
          ad_render_url: "test-ghost-render"
          ad_component_render_urls: "test-ghost-component-render"
          buyer_reporting_id: "ghost-buyer-reporting-id"
          buyer_and_seller_reporting_id: "ghost-buyer-seller-reporting-id"
        }
      )pb",
      &expected_k_anon_ghost_winner));
  ASSERT_NE(k_anon_auction_result_data.kanon_ghost_winners, nullptr);
  ASSERT_EQ(k_anon_auction_result_data.kanon_ghost_winners->size(), 1);
  EXPECT_THAT(k_anon_auction_result_data.kanon_ghost_winners->at(0),
              EqualsProto(expected_k_anon_ghost_winner));
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
