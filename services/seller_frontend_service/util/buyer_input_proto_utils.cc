//  Copyright 2025 Google LLC
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

#include "services/seller_frontend_service/util/buyer_input_proto_utils.h"

#include <string>

#include "rapidjson/document.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::string> ToPrevWinsMs(absl::string_view prev_wins) {
  absl::StatusOr<rapidjson::Document> document = ParseJsonString(prev_wins);
  if (!document.ok() || !document->IsArray()) {
    return absl::InvalidArgumentError(
        "Could not parse array from prev_wins field");
  }

  for (rapidjson::SizeType i = 0; i < document->Size(); i++) {
    rapidjson::Value& innerArray = (*document)[i];
    if (!(innerArray.IsArray() && innerArray.Size() == 2 &&
          innerArray[0].IsInt() && innerArray[1].IsString())) {
      return absl::InvalidArgumentError(
          "prev_wins supplied in an unexpected format");
    }

    int firstValue = innerArray[0].GetInt() * 1000;
    innerArray[0].SetInt(firstValue);
  }

  return SerializeJsonDoc(*document);
}

BrowserSignalsForBidding ToBrowserSignalsForBidding(
    const BrowserSignals& signals) {
  BrowserSignalsForBidding signals_for_bidding;
  signals_for_bidding.set_join_count(signals.join_count());
  signals_for_bidding.set_bid_count(signals.bid_count());
  signals_for_bidding.set_recency(signals.recency());
  signals_for_bidding.set_prev_wins(signals.prev_wins());

  if (signals.has_recency_ms()) {
    signals_for_bidding.set_recency_ms(signals.recency_ms());
  }

  absl::StatusOr<std::string> prev_wins_ms =
      ToPrevWinsMs(signals_for_bidding.prev_wins());
  if (!prev_wins_ms.ok()) {
    PS_VLOG(8) << prev_wins_ms.status();
    return signals_for_bidding;
  }

  signals_for_bidding.set_prev_wins_ms(*prev_wins_ms);
  return signals_for_bidding;
}

BuyerInputForBidding::InterestGroupForBidding ToInterestGroupForBidding(
    BuyerInput::InterestGroup&& interest_group) {
  BuyerInputForBidding::InterestGroupForBidding interest_group_for_bidding;
  interest_group_for_bidding.set_name(
      std::move(*interest_group.mutable_name()));
  interest_group_for_bidding.mutable_bidding_signals_keys()->Swap(
      interest_group.mutable_bidding_signals_keys());
  interest_group_for_bidding.mutable_ad_render_ids()->Swap(
      interest_group.mutable_ad_render_ids());
  interest_group_for_bidding.mutable_component_ads()->Swap(
      interest_group.mutable_component_ads());

  if (!interest_group.user_bidding_signals().empty()) {
    interest_group_for_bidding.set_user_bidding_signals(
        std::move(*interest_group.mutable_user_bidding_signals()));
  }

  if (interest_group.has_browser_signals()) {
    *interest_group_for_bidding.mutable_browser_signals() =
        ToBrowserSignalsForBidding(interest_group.browser_signals());
  } else if (interest_group.has_android_signals()) {
    (void)*interest_group_for_bidding.mutable_android_signals();
  }

  if (!interest_group.origin().empty()) {
    interest_group_for_bidding.set_origin(
        std::move(*interest_group.mutable_origin()));
  }

  return interest_group_for_bidding;
}

BuyerInputForBidding ToBuyerInputForBidding(BuyerInput&& buyer_input) {
  BuyerInputForBidding buyer_input_for_bidding;
  for (auto&& buyer_interest_group : *buyer_input.mutable_interest_groups()) {
    *buyer_input_for_bidding.mutable_interest_groups()->Add() =
        ToInterestGroupForBidding(std::move(buyer_interest_group));
  }

  if (buyer_input.has_protected_app_signals()) {
    buyer_input_for_bidding.mutable_protected_app_signals()->Swap(
        buyer_input.mutable_protected_app_signals());
  }

  buyer_input_for_bidding.set_in_cooldown_or_lockout(
      buyer_input.in_cooldown_or_lockout());

  return buyer_input_for_bidding;
}

BuyerInput ToBuyerInput(const BuyerInputForBidding& buyer_input_for_bidding) {
  BuyerInput buyer_input;

  for (const auto& ig_for_bidding : buyer_input_for_bidding.interest_groups()) {
    BuyerInput::InterestGroup* interest_group =
        buyer_input.add_interest_groups();
    interest_group->set_name(ig_for_bidding.name());

    for (const auto& key : ig_for_bidding.bidding_signals_keys()) {
      interest_group->add_bidding_signals_keys(key);
    }

    for (const auto& ad_render_id : ig_for_bidding.ad_render_ids()) {
      interest_group->add_ad_render_ids(ad_render_id);
    }

    for (const auto& component_ad : ig_for_bidding.component_ads()) {
      interest_group->add_component_ads(component_ad);
    }

    interest_group->set_user_bidding_signals(
        ig_for_bidding.user_bidding_signals());
    interest_group->set_origin(ig_for_bidding.origin());

    if (ig_for_bidding.has_browser_signals()) {
      BrowserSignals* browser_signals =
          interest_group->mutable_browser_signals();
      const BrowserSignalsForBidding& signals_for_bidding =
          ig_for_bidding.browser_signals();
      browser_signals->set_join_count(signals_for_bidding.join_count());
      browser_signals->set_bid_count(signals_for_bidding.bid_count());
      browser_signals->set_recency(signals_for_bidding.recency());
      browser_signals->set_prev_wins(signals_for_bidding.prev_wins());
      if (signals_for_bidding.has_recency_ms()) {
        browser_signals->set_recency_ms(signals_for_bidding.recency_ms());
      }
    } else if (ig_for_bidding.has_android_signals()) {
      (void)*interest_group->mutable_android_signals();
    }
  }

  *buyer_input.mutable_protected_app_signals() =
      buyer_input_for_bidding.protected_app_signals();

  buyer_input.set_in_cooldown_or_lockout(
      buyer_input_for_bidding.in_cooldown_or_lockout());

  return buyer_input;
}

}  // namespace privacy_sandbox::bidding_auction_servers
