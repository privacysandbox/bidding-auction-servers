//   Copyright 2024 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#ifndef SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_UTIL_PROCESS_RESPONSE_H_
#define SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_UTIL_PROCESS_RESPONSE_H_

#include "services/common/clients/http/http_fetcher_async.h"

// Helper methods for building URLs with query params.
namespace privacy_sandbox::bidding_auction_servers {
inline constexpr char kHybridV1OnlyResponseHeaderName[] = "ad-auction-v1-only";

/**
 * Checks if the http reponse indicates that early termination is required
 * and there is no need to call the v2 endpoint
 * @param httpResponse kv server http response
 */
bool IsHybridV1Return(const HTTPResponse& http_response);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_UTIL_PROCESS_RESPONSE_H_
