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

#ifndef SERVICES_COMMON_CLIENTS_KV_SERVER_KV_V2_H_
#define SERVICES_COMMON_CLIENTS_KV_SERVER_KV_V2_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "public/query/v2/get_values_v2.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

struct KVV2AdapterStats {
  int values_with_json_string_parsing = 0;
  int values_without_json_string_parsing = 0;
};
// Convert a TKV V2 response to a serialized json string of JSON-equivalent of
// TKV V1 protocol:
// https://github.com/privacysandbox/protected-auction-key-value-service/blob/release-1.0/public/query/get_values.proto
//
// This is done by going over all compression groups and partitions, and
// grouping all keys by the given key_tags. Note that once B&A will no
// longer need to support both v1 and v2, the prepare function can be reworked
// to potentially not have this conversion or have an optimized version of it.
//
// Example output:
// {
//   "key_tag1" :
//     {
//       "key_a": { "value": "value_a"},
//       "key_c": { "value": "value_b"},
//       ....
//     }
//   "key_tag2" :
//     {
//       "key_d": { "value": "value_e"},
//       "key_f": { "value": "value_g"},
//       ....
//     }
// }
//
// Returns error on a malformed KV V2 Response or
// an empty CompressionGroup.content.
// Returns empty string when the given tags were not found.
absl::StatusOr<std::string> ConvertKvV2ResponseToV1String(
    const std::vector<std::string_view>& key_tags,
    kv_server::v2::GetValuesResponse& v2_response_to_convert,
    KVV2AdapterStats& v2_adapter_stats);

// Determine whether KV V2 should be used.
// CLIENT_TYPE_ANDROID requests MUST go to TKV V2 for TEST_MODE=false.
// CLIENT_TYPE_BROWSER requests go to TKV V2 only if use_tkv_v2_browser is true.
bool UseKvV2(ClientType client_type, bool is_tkv_v2_browser_enabled,
             bool is_test_mode_enabled, bool is_tkv_v2_empty);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_KV_SERVER_KV_V2_H_
