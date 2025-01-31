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
#include "services/common/util/request_response_constants.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

inline constexpr char kLogs[] = "logs";
inline constexpr char kWarnings[] = "warnings";
inline constexpr char kErrors[] = "errors";
inline constexpr int kNumDebugReportingReplacements = 5;
inline constexpr int kNumAdditionalWinReportingReplacements = 2;
inline constexpr absl::string_view kFeatureDisabled = "false";
inline constexpr absl::string_view kFeatureEnabled = "true";

void MayVlogAdTechCodeLogs(const rapidjson::Document& document,

                           const std::string& log_type,
                           RequestLogContext& log_context) {
  auto logs_it = document.FindMember(log_type.c_str());
  if (logs_it != document.MemberEnd()) {
    for (const auto& log : logs_it->value.GetArray()) {
      std::string log_str = absl::StrCat(log_type, ": ", log.GetString());
      PS_VLOG(kUdfLog, log_context) << log_str;
      log_context.SetEventMessageField(log_str);
    }
  }
}

void AppendFeatureFlagValue(std::string& feature_flags,
                            absl::string_view feature_name,
                            bool is_feature_enabled) {
  absl::string_view enable_feature = kFeatureDisabled;
  if (is_feature_enabled) {
    enable_feature = kFeatureEnabled;
  }
  feature_flags.append(
      absl::StrCat("\"", feature_name, "\": ", enable_feature));
}

}  // namespace

PostAuctionSignals GeneratePostAuctionSignalsForTopLevelSeller(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score) {
  // If there is no winning ad, return with default signals values.
  if (!winning_ad_score) {
    return {kDefaultWinningInterestGroupName,
            kDefaultWinningInterestGroupOwner,
            kDefaultWinningBid,
            /*DefaultWinningBidCurrency=*/kUnknownBidCurrencyCode,
            /*winning_bid_in_seller_currency=*/kDefaultWinningBid,
            kUnknownBidCurrencyCode,
            /*seller_data_version=*/0,
            /*DefaultHighestScoringOtherBid=*/0.0,
            /*DefaultHighestScoringOtherBidCurrency=*/kUnknownBidCurrencyCode,
            kDefaultHighestScoringOtherBidInterestGroupOwner,
            /*DefaultHasHighestScoringOtherBid=*/false,
            kDefaultWinningScore,
            kDefaultWinningAdRenderUrl,
            {}};
  }
  float winning_bid = winning_ad_score->buyer_bid();
  float winning_score = winning_ad_score->desirability();

  return {winning_ad_score->interest_group_name(),
          winning_ad_score->interest_group_owner(),
          winning_bid,
          /*DefaultWinningBidCurrency=*/kUnknownBidCurrencyCode,
          /*winning_bid_in_seller_currency=*/kDefaultWinningBid,
          kUnknownBidCurrencyCode,
          /*seller_data_version=*/0,
          /*DefaultHighestScoringOtherBid=*/0.0,
          /*DefaultHighestScoringOtherBidCurrency=*/kUnknownBidCurrencyCode,
          kDefaultHighestScoringOtherBidInterestGroupOwner,
          /*DefaultHasHighestScoringOtherBid=*/false,
          winning_score,
          winning_ad_score->render(),
          {}};
}

PostAuctionSignals GeneratePostAuctionSignals(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score,
    std::string seller_currency, const uint32_t seller_data_version) {
  // If there is no winning ad, return with default signals values.
  if (!winning_ad_score) {
    return {kDefaultWinningInterestGroupName,
            kDefaultWinningInterestGroupOwner,
            kDefaultWinningBid,
            /*DefaultWinningBidCurrency=*/kUnknownBidCurrencyCode,
            /*winning_bid_in_seller_currency=*/kDefaultWinningBid,
            std::move(seller_currency),
            seller_data_version,
            /*DefaultHighestScoringOtherBid=*/0.0,
            /*DefaultHighestScoringOtherBidCurrency=*/kUnknownBidCurrencyCode,
            kDefaultHighestScoringOtherBidInterestGroupOwner,
            /*DefaultHasHighestScoringOtherBid=*/false,
            kDefaultWinningScore,
            kDefaultWinningAdRenderUrl,
            {}};
  }
  float winning_bid = winning_ad_score->buyer_bid();
  float winning_bid_in_seller_currency =
      winning_ad_score->incoming_bid_in_seller_currency();
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

  std::string bid_currency = (winning_ad_score->buyer_bid_currency().empty())
                                 ? kUnknownBidCurrencyCode
                                 : winning_ad_score->buyer_bid_currency();
  std::string highest_scoring_other_bid_currency =
      (seller_currency.empty()) ? kUnknownBidCurrencyCode : seller_currency;

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
          bid_currency,
          winning_bid_in_seller_currency,
          std::move(seller_currency),
          seller_data_version,
          highest_scoring_other_bid,
          highest_scoring_other_bid_currency,
          std::move(highest_scoring_other_bid_ig_owner),
          has_highest_scoring_other_bid,
          winning_score,
          winning_ad_score->render(),
          std::move(rejection_reason_map),
          made_highest_scoring_other_bid};
}

HTTPRequest CreateDebugReportingHttpRequest(
    absl::string_view url, const DebugReportingPlaceholder& placeholder_data,
    bool is_winning_interest_group) {
  HTTPRequest http_request;
  http_request.url = CreateDebugReportingUrlForInterestGroup(
      url, placeholder_data, is_winning_interest_group);
  http_request.headers = {};
  return http_request;
}

std::string CreateDebugReportingUrlForInterestGroup(
    absl::string_view debug_url,
    const DebugReportingPlaceholder& placeholder_data,
    bool is_winning_interest_group) {
  std::vector<std::pair<absl::string_view, std::string>> replacements;
  replacements.reserve(
      kNumDebugReportingReplacements +
      (is_winning_interest_group ? kNumAdditionalWinReportingReplacements : 0));
  replacements.push_back(
      {kWinningBidPlaceholder,
       absl::StrFormat("%.2f", placeholder_data.winning_bid)});
  replacements.push_back(
      {kWinningBidCurrencyPlaceholder, placeholder_data.winning_bid_currency});
  replacements.push_back(
      {kMadeWinningBidPlaceholder,
       placeholder_data.made_winning_bid ? "true" : "false"});
  replacements.push_back(
      {kRejectReasonPlaceholder, std::string(ToSellerRejectionReasonString(
                                     placeholder_data.rejection_reason))});
  replacements.push_back(
      {kHighestScoringOtherBidPlaceholder, kDefaultHighestScoringOtherBid});
  replacements.push_back({kHighestScoringOtherBidCurrencyPlaceholder,
                          placeholder_data.highest_scoring_other_bid_currency});
  replacements.push_back({kMadeHighestScoringOtherBidPlaceholder,
                          kDefaultHasHighestScoringOtherBid});
  // Only pass the second highest scored bid information to the winner.
  if (is_winning_interest_group) {
    replacements.push_back(
        {kHighestScoringOtherBidPlaceholder,
         absl::StrFormat("%.2f", placeholder_data.highest_scoring_other_bid)});
    replacements.push_back(
        {kMadeHighestScoringOtherBidPlaceholder,
         placeholder_data.made_highest_scoring_other_bid ? "true" : "false"});
  }
  return absl::StrReplaceAll(debug_url, replacements);
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
  float winning_bid;
  absl::string_view both_currency_codes;
  if (!post_auction_signals.seller_currency.empty()) {
    winning_bid = post_auction_signals.winning_bid_in_seller_currency;
    // In DebugReporting, when seller currency is set, all currency markers are
    // set to it.
    both_currency_codes = post_auction_signals.seller_currency;
  } else {
    // In the PostAuctionSignals, the winning bid is always the original
    // buyer_bid.
    winning_bid = post_auction_signals.winning_bid;
    // In DebugReporting, when seller currency is un-set, all currency markers
    // are set to Unknown.
    both_currency_codes = kUnknownBidCurrencyCode;
  }
  return {
      .winning_bid = winning_bid,
      .winning_bid_currency = std::string(both_currency_codes),
      .made_winning_bid = made_winning_bid,
      // The PostAuctionSignals just retrieves and sets the
      // highest_scoring_other_bid. highest_scoring_other_bid was already set to
      // the original value if no seller_currency, and the modified value if
      // some seller currency.
      .highest_scoring_other_bid =
          post_auction_signals.highest_scoring_other_bid,
      .highest_scoring_other_bid_currency = std::string(both_currency_codes),
      .made_highest_scoring_other_bid = made_highest_scoring_other_bid,
      .rejection_reason = rejection_reason};
}

SellerRejectionReason ToSellerRejectionReason(
    absl::string_view rejection_reason_str) {
  if (rejection_reason_str.empty()) {
    return SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE;
  }

  if (kRejectionReasonInvalidBid == rejection_reason_str) {
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
  } else if (kRejectionReasonDidNotMeetTheKAnonymityThreshold ==
             rejection_reason_str) {
    return SellerRejectionReason::DID_NOT_MEET_THE_KANONYMITY_THRESHOLD;
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
    case SellerRejectionReason::DID_NOT_MEET_THE_KANONYMITY_THRESHOLD:
      return kRejectionReasonDidNotMeetTheKAnonymityThreshold;
    default:
      return kRejectionReasonNotAvailable;
  }
}

void MayVlogAdTechCodeLogs(bool enable_ad_tech_code_logging,
                           const rapidjson::Document& document,
                           RequestLogContext& log_context) {
  if (!enable_ad_tech_code_logging) {
    return;
  }

  MayVlogAdTechCodeLogs(document, kLogs, log_context);
  MayVlogAdTechCodeLogs(document, kWarnings, log_context);
  MayVlogAdTechCodeLogs(document, kErrors, log_context);
}

absl::StatusOr<std::string> ParseAndGetResponseJson(
    bool enable_ad_tech_code_logging, const std::string& response,
    RequestLogContext& log_context) {
  PS_VLOG(5) << "Parsing with logging enabled: " << enable_ad_tech_code_logging;
  PS_ASSIGN_OR_RETURN(rapidjson::Document document, ParseJsonString(response));
  MayVlogAdTechCodeLogs(enable_ad_tech_code_logging, document, log_context);
  return SerializeJsonDoc(document["response"]);
}

absl::StatusOr<std::vector<std::string>> ParseAndGetResponseJsonArray(
    bool enable_ad_tech_code_logging, const std::string& response,
    RequestLogContext& log_context) {
  PS_ASSIGN_OR_RETURN(rapidjson::Document document, ParseJsonString(response));
  if (enable_ad_tech_code_logging) {
    PS_VLOG(5) << "Parsing with logging enabled: "
               << enable_ad_tech_code_logging;
    MayVlogAdTechCodeLogs(enable_ad_tech_code_logging, document, log_context);
    return SerializeJsonArrayDocToVector(document["response"]);
  } else {
    return SerializeJsonArrayDocToVector(document);
  }
}

std::string GetFeatureFlagJson(bool enable_logging,
                               bool enable_debug_url_generation) {
  std::string feature_flags = "{";
  AppendFeatureFlagValue(feature_flags, kFeatureLogging, enable_logging);
  feature_flags.append(",");
  AppendFeatureFlagValue(feature_flags, kFeatureDebugUrlGeneration,
                         enable_debug_url_generation);
  feature_flags.append("}");
  return feature_flags;
}

}  // namespace privacy_sandbox::bidding_auction_servers
