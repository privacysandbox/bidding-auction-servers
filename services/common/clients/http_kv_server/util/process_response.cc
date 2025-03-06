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

#include "services/common/clients/http_kv_server/util/process_response.h"

namespace privacy_sandbox::bidding_auction_servers {

bool IsHybridV1Return(const HTTPResponse& http_response) {
  if (auto dv_header_it =
          http_response.headers.find(kHybridV1OnlyResponseHeaderName);
      dv_header_it != http_response.headers.end()) {
    if (const auto& dv_header = dv_header_it->second; dv_header.ok()) {
      return *dv_header == "true";
    }
  }
  return false;
}

}  // namespace privacy_sandbox::bidding_auction_servers
