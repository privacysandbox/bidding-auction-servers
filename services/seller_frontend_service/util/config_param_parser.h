//   Copyright 2022 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CONFIG_PARAM_PARSER_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CONFIG_PARAM_PARSER_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
namespace privacy_sandbox::bidding_auction_servers {

// Parses a JSON string containing a map of IG Owner to BFE Domain address
// into an absl::flat_hash_map, which is what the
// BuyerFrontendAsyncClientFactory constructor expects. Used to parse the
// startup config parameter in seller_frontend_main. Parsing may fail, in which
// case the server should not start, hence the StatusOr. If the input to Factory
// constructor changes, so should ths function.
absl::StatusOr<absl::flat_hash_map<std::string, std::string>>
ParseIgOwnerToBfeDomainMap(absl::string_view ig_owner_to_bfe_domain);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CONFIG_PARAM_PARSER_H_