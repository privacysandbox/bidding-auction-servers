//  Copyright 2024 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_UTILS_H_

#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/escaping.h"
#include "services/common/loggers/request_log_context.h"
#include "services/seller_frontend_service/data/k_anon.h"
#include "services/seller_frontend_service/report_win_map.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr auto formatter = [](std::string* out, absl::string_view hash) {
  absl::StrAppend(out, absl::BytesToHexString(hash));
};

constexpr char kEmptyString[] = "";

// Creates KAnonJoinCandidate proto message based on the passed in ad score
// and report win map. This method is used for populating the k-anon join
// candidates for web as well as the top level server orchestrated multi-seller
// auction.
KAnonJoinCandidate GetKAnonJoinCandidate(const ScoreAdsResponse::AdScore& score,
                                         const ReportWinMap& report_win_map);

// Creates winner / ghost winner k-anon based on provided inputs. The returned
// data can then be used to populate corresponding proto messages in
// AuctionResult. This method is used for single seller web auctions.
KAnonAuctionResultData GetKAnonAuctionResultData(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const google::protobuf::RepeatedPtrField<ScoreAdsResponse::AdScore>*
        ghost_winning_scores,
    bool is_component_auction,
    absl::AnyInvocable<KAnonJoinCandidate(const ScoreAdsResponse::AdScore&)>
        get_kanon_join_candidate,
    RequestLogContext& log_context);

// Creates winner / ghost winner k-anon based on provided inputs. The returned
// data can then be used to populate corresponding proto messages in
// AuctionResult of top level server orchestrated auction.
KAnonAuctionResultData GetKAnonAuctionResultData(
    ScoreAdsResponse::AdScore* high_score,
    google::protobuf::RepeatedPtrField<ScoreAdsResponse::AdScore>&
        ghost_winning_scores,
    RequestLogContext& log_context);

// Converts the k-anon hash sets (bytes) to hex encoded comma separated stirng.
inline std::string KAnonHashSetsToString(
    const absl::flat_hash_set<std::string>& hashes) {
  return absl::StrJoin(hashes, ", ", formatter);
}

inline std::string KAnonHashSetsToString(
    const absl::flat_hash_set<absl::string_view>& hashes) {
  return absl::StrJoin(hashes, ", ", formatter);
}

// Converts map to set using the maps' keys.
inline absl::flat_hash_set<std::string> MapToSet(
    const absl::flat_hash_map<std::string, std::string>& source) {
  absl::flat_hash_set<std::string> dest;
  for (const auto& entry : source) {
    dest.insert(entry.first);
  }

  return dest;
}

// Converts set to map, using an empty string for all entry values.
inline absl::flat_hash_map<std::string, std::string> SetToMap(
    const absl::flat_hash_set<std::string>& source) {
  absl::flat_hash_map<std::string, std::string> dest;
  std::transform(
      source.begin(), source.end(), std::inserter(dest, dest.end()),
      [](const std::string& s) { return std::make_pair(s, kEmptyString); });
  return dest;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_UTILS_H_
