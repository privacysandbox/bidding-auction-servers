//  Copyright 2023 Google LLC
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

#include "services/common/util/reporting_util.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "services/common/util/json_util.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

inline constexpr char kLogs[] = "logs";
inline constexpr char kWarnings[] = "warnings";
inline constexpr char kErrors[] = "errors";
inline constexpr int kNumDebugReportingReplacements = 5;
inline constexpr int kNumAdditionalWinReportingReplacements = 2;

void MayVlogAdTechCodeLogs(const rapidjson::Document& document,

                           const std::string& log_type,
                           server_common::log::ContextImpl& log_context) {
  auto logs_it = document.FindMember(log_type.c_str());
  if (logs_it != document.MemberEnd()) {
    for (const auto& log : logs_it->value.GetArray()) {
      PS_VLOG(1, log_context) << log_type << ": " << log.GetString();
    }
  }
}

}  // namespace

PostAuctionSignals GeneratePostAuctionSignals(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score) {
  // If there is no winning ad, return with default signals values.
  if (!winning_ad_score.has_value()) {
    return {kDefaultWinningInterestGroupName,
            kDefaultWinningInterestGroupOwner,
            kDefaultWinningBid,
            /*DefaultHighestScoringOtherBid=*/0.0,
            kDefaultHighestScoringOtherBidInterestGroupOwner,
            /*DefaultHasHighestScoringOtherBid=*/false,
            kDefaultWinningScore,
            kDefaultWinningAdRenderUrl,
            {}};
  }
  float winning_bid = winning_ad_score->buyer_bid();
  float winning_score = winning_ad_score->desirability();
  // Set second highest other bid information in signals if available.
  std::string highest_scoring_other_bid_ig_owner =
      kDefaultHighestScoringOtherBidInterestGroupOwner;
  float highest_scoring_other_bid = 0.0;
  bool has_highest_scoring_other_bid = false;
  if (winning_ad_score->ig_owner_highest_scoring_other_bids_map().size() > 0) {
    auto iterator =
        winning_ad_score->ig_owner_highest_scoring_other_bids_map().begin();
    if (iterator->second.values().size() > 0) {
      highest_scoring_other_bid_ig_owner = iterator->first;
      highest_scoring_other_bid =
          iterator->second.values().Get(0).number_value();
      has_highest_scoring_other_bid = true;
    }
  }

  bool made_highest_scoring_other_bid = false;
  if (winning_ad_score->ig_owner_highest_scoring_other_bids_map().size() == 1 &&
      winning_ad_score->ig_owner_highest_scoring_other_bids_map().contains(
          winning_ad_score->interest_group_owner())) {
    made_highest_scoring_other_bid = true;
  }

  // group rejection reasons by buyer and interest group owner.
  absl::flat_hash_map<std::string,
                      absl::flat_hash_map<std::string, SellerRejectionReason>>
      rejection_reason_map;
  for (const auto& ad_rejection_reason :
       winning_ad_score->ad_rejection_reasons()) {
    SellerRejectionReason interest_group_rejection_reason =
        ad_rejection_reason.rejection_reason();
    if (rejection_reason_map.contains(
            ad_rejection_reason.interest_group_owner())) {
      rejection_reason_map.find(ad_rejection_reason.interest_group_owner())
          ->second.try_emplace(ad_rejection_reason.interest_group_name(),
                               interest_group_rejection_reason);
    } else {
      absl::flat_hash_map<std::string, SellerRejectionReason> ig_rejection_map;
      ig_rejection_map.emplace(ad_rejection_reason.interest_group_name(),
                               interest_group_rejection_reason);
      rejection_reason_map.emplace(ad_rejection_reason.interest_group_owner(),
                                   ig_rejection_map);
    }
  }
  return {winning_ad_score->interest_group_name(),
          winning_ad_score->interest_group_owner(),
          winning_bid,
          highest_scoring_other_bid,
          std::move(highest_scoring_other_bid_ig_owner),
          has_highest_scoring_other_bid,
          winning_score,
          winning_ad_score->render(),
          std::move(rejection_reason_map),
          made_highest_scoring_other_bid};
}

HTTPRequest CreateDebugReportingHttpRequest(
    absl::string_view url, const DebugReportingPlaceholder& placeholder_data,
    bool is_win_debug_url) {
  std::vector<std::pair<absl::string_view, std::string>> replacements;
  replacements.reserve(
      kNumDebugReportingReplacements +
      (is_win_debug_url ? kNumAdditionalWinReportingReplacements : 0));
  replacements.push_back(
      {kWinningBidPlaceholder,
       absl::StrFormat("%.2f", placeholder_data.winning_bid)});
  replacements.push_back(
      {kMadeWinningBidPlaceholder,
       placeholder_data.made_winning_bid ? "true" : "false"});
  replacements.push_back(
      {kRejectReasonPlaceholder, std::string(ToSellerRejectionReasonString(
                                     placeholder_data.rejection_reason))});
  replacements.push_back(
      {kHighestScoringOtherBidPlaceholder, kDefaultHighestScoringOtherBid});
  replacements.push_back({kMadeHighestScoringOtherBidPlaceholder,
                          kDefaultHasHighestScoringOtherBid});
  // Only pass the second highest scored bid information to the winner.
  if (is_win_debug_url) {
    replacements.push_back(
        {kHighestScoringOtherBidPlaceholder,
         absl::StrFormat("%.2f", placeholder_data.highest_scoring_other_bid)});
    replacements.push_back(
        {kMadeHighestScoringOtherBidPlaceholder,
         placeholder_data.made_highest_scoring_other_bid ? "true" : "false"});
  }
  HTTPRequest http_request;
  http_request.url = absl::StrReplaceAll(url, replacements);
  http_request.headers = {};
  return http_request;
}

DebugReportingPlaceholder GetPlaceholderDataForInterestGroup(
    absl::string_view interest_group_owner,
    absl::string_view interest_group_name,
    const PostAuctionSignals& post_auction_signals) {
  bool made_winning_bid =
      interest_group_owner == post_auction_signals.winning_ig_owner;
  bool made_highest_scoring_other_bid =
      post_auction_signals.has_highest_scoring_other_bid &&
      interest_group_owner ==
          post_auction_signals.highest_scoring_other_bid_ig_owner;
  SellerRejectionReason rejection_reason =
      SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE;
  if (auto ig_owner_itr =
          post_auction_signals.rejection_reason_map.find(interest_group_owner);
      ig_owner_itr != post_auction_signals.rejection_reason_map.end()) {
    if (auto ig_name_itr = ig_owner_itr->second.find(interest_group_name);
        ig_name_itr != ig_owner_itr->second.end()) {
      rejection_reason = ig_name_itr->second;
    }
  }
  return {.winning_bid = post_auction_signals.winning_bid,
          .made_winning_bid = made_winning_bid,
          .highest_scoring_other_bid =
              post_auction_signals.highest_scoring_other_bid,
          .made_highest_scoring_other_bid = made_highest_scoring_other_bid,
          .rejection_reason = rejection_reason};
}

SellerRejectionReason ToSellerRejectionReason(
    absl::string_view rejection_reason_str) {
  if (rejection_reason_str.empty()) {
    return SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE;
  } else if (kRejectionReasonInvalidBid == rejection_reason_str) {
    return SellerRejectionReason::INVALID_BID;
  } else if (kRejectionReasonBidBelowAuctionFloor == rejection_reason_str) {
    return SellerRejectionReason::BID_BELOW_AUCTION_FLOOR;
  } else if (kRejectionReasonPendingApprovalByExchange ==
             rejection_reason_str) {
    return SellerRejectionReason::PENDING_APPROVAL_BY_EXCHANGE;
  } else if (kRejectionReasonDisapprovedByExchange == rejection_reason_str) {
    return SellerRejectionReason::DISAPPROVED_BY_EXCHANGE;
  } else if (kRejectionReasonBlockedByPublisher == rejection_reason_str) {
    return SellerRejectionReason::BLOCKED_BY_PUBLISHER;
  } else if (kRejectionReasonLanguageExclusions == rejection_reason_str) {
    return SellerRejectionReason::LANGUAGE_EXCLUSIONS;
  } else if (kRejectionReasonCategoryExclusions == rejection_reason_str) {
    return SellerRejectionReason::CATEGORY_EXCLUSIONS;
  } else if (kRejectionReasonBidFromGenBidFailedCurrencyCheck ==
             rejection_reason_str) {
    return SellerRejectionReason::BID_FROM_GENERATE_BID_FAILED_CURRENCY_CHECK;
  } else if (kRejectionReasonBidFromScoreAdFailedCurrencyCheck ==
             rejection_reason_str) {
    return SellerRejectionReason::BID_FROM_SCORE_AD_FAILED_CURRENCY_CHECK;
  } else {
    return SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE;
  }
}

absl::string_view ToSellerRejectionReasonString(
    SellerRejectionReason rejection_reason) {
  switch (rejection_reason) {
    case SellerRejectionReason::INVALID_BID:
      return kRejectionReasonInvalidBid;
    case SellerRejectionReason::BID_BELOW_AUCTION_FLOOR:
      return kRejectionReasonBidBelowAuctionFloor;
    case SellerRejectionReason::PENDING_APPROVAL_BY_EXCHANGE:
      return kRejectionReasonPendingApprovalByExchange;
    case SellerRejectionReason::DISAPPROVED_BY_EXCHANGE:
      return kRejectionReasonDisapprovedByExchange;
    case SellerRejectionReason::BLOCKED_BY_PUBLISHER:
      return kRejectionReasonBlockedByPublisher;
    case SellerRejectionReason::LANGUAGE_EXCLUSIONS:
      return kRejectionReasonLanguageExclusions;
    case SellerRejectionReason::CATEGORY_EXCLUSIONS:
      return kRejectionReasonCategoryExclusions;
    case SellerRejectionReason::BID_FROM_GENERATE_BID_FAILED_CURRENCY_CHECK:
      return kRejectionReasonBidFromGenBidFailedCurrencyCheck;
    case SellerRejectionReason::BID_FROM_SCORE_AD_FAILED_CURRENCY_CHECK:
      return kRejectionReasonBidFromScoreAdFailedCurrencyCheck;
    default:
      return kRejectionReasonNotAvailable;
  }
}

void MayVlogAdTechCodeLogs(bool enable_ad_tech_code_logging,
                           const rapidjson::Document& document,
                           server_common::log::ContextImpl& log_context) {
  if (!enable_ad_tech_code_logging) {
    return;
  }

  MayVlogAdTechCodeLogs(document, kLogs, log_context);
  MayVlogAdTechCodeLogs(document, kWarnings, log_context);
  MayVlogAdTechCodeLogs(document, kErrors, log_context);
}

absl::StatusOr<std::string> ParseAndGetResponseJson(
    bool enable_ad_tech_code_logging, const std::string& response,
    server_common::log::ContextImpl& log_context) {
  PS_ASSIGN_OR_RETURN(rapidjson::Document document, ParseJsonString(response));
  MayVlogAdTechCodeLogs(enable_ad_tech_code_logging, document, log_context);
  return SerializeJsonDoc(document["response"]);
}

}  // namespace privacy_sandbox::bidding_auction_servers
