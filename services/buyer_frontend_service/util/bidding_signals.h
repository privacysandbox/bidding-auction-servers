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

#ifndef FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_UTIL_BIDDING_SIGNALS_H_
#define FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_UTIL_BIDDING_SIGNALS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "rapidjson/document.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kGetBiddingSignalsSuccessButEmpty =
    "GetBiddingSignals request succeeded but was empty.";
inline constexpr absl::string_view kBiddingSignalsJsonNotParseable =
    "Could not parse trusted signals json.";
inline constexpr absl::string_view kBiddingSignalsJsonMissingKeysProperty =
    "Trusted bidding signals JSON validate error (Missing or non-object "
    "property "
    "\"keys\")";

// This serves as a wrapper struct that holds onto various portions of
// the kv server response until the moment they are needed, at which
// point they must be moved from this struct and should never again be
// referenced on this struct.
struct BiddingSignalJsonComponents {
  // Represents the 'keys' object in the KV response.
  // This field MUST be moved from because it may be large in size,
  // so we only try to keep it in memory until we do not need it.
  // Do not rely on this existing after a GenerateBidsRequest has
  // been sent.
  std::unique_ptr<rapidjson::Value> bidding_signals;

  // This field represents the buyer's interest groups that should be
  // udpated on device. This field will be moved from; it must be sent
  // back to the SFE in the GetBidsResponse.
  UpdateInterestGroupList update_igs;

  // The size of the request; this is used to calculate the average
  // amount of information per interest group.
  // This field may be copied directly instead of being moved.
  size_t raw_size;

  // The parent document from which the above Values are spawned.
  // This must only be freed after
  // bidding_signals and per_interest_group_data are freed.
  // The parent Document owns the Allocator responsible for the memory
  // backing the Values' data.
  rapidjson::Document bidding_signals_document;
};

// Given a BiddingSignals resopnse from the KV server, we parse
// the response into its various subcomponents that will be used at
// different stages in the bidding process. This function also performs
// basic validations.
absl::StatusOr<BiddingSignalJsonComponents> ParseTrustedBiddingSignals(
    std::unique_ptr<BiddingSignals> bidding_signals,
    const BuyerInput& buyer_input);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_UTIL_BIDDING_SIGNALS_H_
