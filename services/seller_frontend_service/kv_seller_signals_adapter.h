/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_KV_SELLER_SIGNALS_ADAPTER_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_KV_SELLER_SIGNALS_ADAPTER_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "public/query/v2/get_values_v2.pb.h"
#include "services/common/clients/kv_server/kv_v2.h"
#include "services/seller_frontend_service/providers/scoring_signals_async_provider.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kSellerKvClientVersion[] = "Bna.PA.Seller.20240930";

using kv_server::v2::GetValuesRequest;
using kv_server::v2::GetValuesResponse;

// Create a TKV v2 request given a map of bids.
// Current implementation creates a partition for each bid. This can be improved
// by combining multiple bids into a single partition. This would result in a
// better performance on TKV side, since each partition is a single UDF
// execution. Each UDF execution has overhead.
absl::StatusOr<std::unique_ptr<GetValuesRequest>> CreateV2ScoringRequest(
    const ScoringSignalsRequest& scoring_signals_request, bool is_pas_enabled,
    std::optional<server_common::ConsentedDebugConfiguration>
        consented_debug_config = std::nullopt);

// Convert a TKV V2 response to a serialized json string of JSON-equivalent of
// TKV V1 protocol:
// https://github.com/privacysandbox/protected-auction-key-value-service/blob/release-1.0/public/query/get_values.proto
//
// {
//   "renderUrls" :
//     {
//       "key_a": { "value": "value_a"},
//       "key_c": { "value": "value_b"},
//       ....
//     }
//   "adComponentRenderUrls" :
//     {
//       "key_d": { "value": "value_e"},
//       "key_f": { "value": "value_g"},
//       ....
//     }
// }
// This is done by going over all compression groups and partitions, and
// grouping all "renderUrls" and all "adComponentRenderUrls". Note that once B&A
// will no longer need to support both v1 and v2, the prepare function can be
// reworked to potentially not have this conversion or have an optimized version
// of it.
absl::StatusOr<std::unique_ptr<ScoringSignals>>
ConvertV2ResponseToV1ScoringSignals(std::unique_ptr<GetValuesResponse> response,
                                    KVV2AdapterStats& v2_adapter_stats);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_KV_SELLER_SIGNALS_ADAPTER_H_
