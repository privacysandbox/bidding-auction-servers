// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/buyer_frontend_service/util/bidding_signals.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "rapidjson/document.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/common/util/json_util.h"
#include "services/common/util/priority_vector/priority_vector_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
// Given the per interest group data, attempt to parse the data.
// If incorrect types are found, simply skip over them -- this function
// tries to return any result as flexibly as possible.
// Adtechs may be informed of errors in the KV response via metrics.
void ParsePerInterestGroupData(rapidjson::Value& per_interest_group_data,
                               const BuyerInputForBidding& buyer_input,
                               BiddingSignalJsonComponents& result) {
  if (!per_interest_group_data.IsObject()) {
    return;
  }

  for (int i = 0; i < buyer_input.interest_groups_size(); i++) {
    const BuyerInputForBidding::InterestGroupForBidding& interest_group =
        buyer_input.interest_groups(i);
    if (interest_group.name().empty()) {
      continue;
    }

    auto ig_itr =
        per_interest_group_data.FindMember(interest_group.name().c_str());
    if (ig_itr == per_interest_group_data.MemberEnd()) {
      continue;
    }

    rapidjson::Value& interest_group_doc = ig_itr->value;
    auto update_itr =
        interest_group_doc.FindMember(kUpdateIfOlderThanMsStr.data());
    if (update_itr != interest_group_doc.MemberEnd()) {
      const rapidjson::Value& update = update_itr->value;
      if (update.IsUint()) {
        UpdateInterestGroup update_ig;
        update_ig.set_index(i);
        update_ig.set_update_if_older_than_ms(update.GetUint());
        *result.update_igs.mutable_interest_groups()->Add() =
            std::move(update_ig);
      }  // TODO(b/308793587): Publish a metric if !update.IsUint.
    }

    auto pv_itr = interest_group_doc.FindMember(kPriorityVector.data());
    rapidjson::Value& priority_vector = pv_itr->value;
    if (pv_itr != interest_group_doc.MemberEnd() &&
        priority_vector.IsObject()) {
      SanitizePriorityVector(priority_vector);
      result.per_ig_priority_vectors[interest_group.name()] =
          std::move(priority_vector);
    }  // TODO(b/308793587): Publish a metric if !pv.IsObject.
  }
}
}  // namespace

absl::StatusOr<BiddingSignalJsonComponents> ParseTrustedBiddingSignals(
    std::unique_ptr<BiddingSignals> bidding_signals,
    const BuyerInputForBidding& buyer_input) {
  if (!bidding_signals || !bidding_signals->trusted_signals ||
      bidding_signals->trusted_signals->empty()) {
    return absl::InvalidArgumentError(kGetBiddingSignalsSuccessButEmpty);
  }
  BiddingSignalJsonComponents result;
  result.raw_size = bidding_signals->trusted_signals->size();

  // Deserialize trusted bidding signals.
  absl::StatusOr<rapidjson::Document> trusted_signals_status =
      ParseJsonString(*bidding_signals->trusted_signals);
  rapidjson::Document trusted_signals;
  if (trusted_signals_status.ok()) {
    trusted_signals = *std::move(trusted_signals_status);
  } else {
    // TODO(b/308793587): Publish a metric for this error.
    return absl::InternalError(kBiddingSignalsJsonNotParseable);
  }

  auto per_ig_itr = trusted_signals.FindMember(kPerInterestGroupData.data());
  if (per_ig_itr != trusted_signals.MemberEnd()) {
    ParsePerInterestGroupData(per_ig_itr->value, buyer_input, result);
  }

  rapidjson::Value::MemberIterator keys_itr =
      trusted_signals.FindMember(kKeys.data());
  if (keys_itr != trusted_signals.MemberEnd() && keys_itr->value.IsObject()) {
    result.bidding_signals =
        std::make_unique<rapidjson::Value>(std::move(keys_itr->value));
  } else {
    return absl::InvalidArgumentError(kBiddingSignalsJsonMissingKeysProperty);
  }

  result.bidding_signals_document = std::move(trusted_signals);
  return result;
}

}  // namespace privacy_sandbox::bidding_auction_servers
