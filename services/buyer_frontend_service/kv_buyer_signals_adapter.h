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

#ifndef SERVICES_BUYER_FRONTEND_SERVICE_KV_BUYER_SIGNALS_ADAPTER_H_
#define SERVICES_BUYER_FRONTEND_SERVICE_KV_BUYER_SIGNALS_ADAPTER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "public/query/v2/get_values_v2.pb.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/buyer_frontend_service/providers/bidding_signals_async_provider.h"
#include "services/common/clients/kv_server/kv_v2.h"

using kv_server::v2::GetValuesResponse;

namespace privacy_sandbox::bidding_auction_servers {
// Convert a TKV V2 response to a serialized json string of format that
// `PrepareAndGenerateProtectedAudienceBid` expects.
// {
//   "keys" :
//     {
//       "key_a": { "value": "value_a"},
//       "key_c": { "value": "value_b"},
//       ....
//     }
// }
// This is done by going over all compression groups and partitions, and merging
// all keys, tagged as keys, together.
// Note that once B&A will no longer need to support both v1 and v2, the prepare
// function can be reworked to potentially not have this conversion or have an
// optimized version of it.
absl::StatusOr<std::unique_ptr<BiddingSignals>> ConvertV2BiddingSignalsToV1(
    std::unique_ptr<kv_server::v2::GetValuesResponse> response,
    KVV2AdapterStats& v2_adapter_stats);

// Create a TKV v2 request given a list of IG keys.
// Current implementation creates a partition for each IG. This can be improved
// by combining multiple IGs into a single partition. This would result in a
// better performance on TKV side, since each partition is a single UDF
// execution. Each UDF execution has overhead. Note that unlike the current B&A
// v1 implementation, we are not passing IG names, since we don't really need
// those on TKV side to do the keys lookup.
absl::StatusOr<std::unique_ptr<kv_server::v2::GetValuesRequest>>
CreateV2BiddingRequest(const BiddingSignalsRequest& bidding_signals_request,
                       bool propagate_buyer_signals_to_tkv = false,
                       std::unique_ptr<std::string> byos_output = nullptr);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_FRONTEND_SERVICE_KV_BUYER_SIGNALS_ADAPTER_H_
