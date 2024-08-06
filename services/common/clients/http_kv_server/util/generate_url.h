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

#ifndef SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_UTIL_GENERATE_URL_H_
#define SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_UTIL_GENERATE_URL_H_

#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/strings/str_cat.h"

// Helper methods for building URLs with query params.
namespace privacy_sandbox::bidding_auction_servers {

using UrlKeysSet = absl::btree_set<absl::string_view>;

/**
 * Adds an ampersand if one is needed, and does nothing if one is not needed.
 * @param url the url being built
 * NOTE: Every time a new query param is added, call this method.
 */
void AddAmpersandIfNotFirstQueryParam(std::string* url);

/**
 * Appends the key and its values into the url.
 * Example example output from this function: "exmaple.com?fruits=apple,orange"
 * @param url the url being built, e.g. "example.com"
 * @param key the name of the query parameter, e.g. "fruits"
 * @param values the values for the query parameter, e.g. ["apple", "orange"]
 * @param encode_params the query parameters will be http encoded (eg. for URLs)
 * NOTE: When adding a new query parameter with multiple values, call this
 * method.
 */
void AddListItemsAsQueryParamsToUrl(std::string* url, absl::string_view key,
                                    const UrlKeysSet& values,
                                    bool encode_params = false);

/**
 * Clears the string when creating a new url, adds the host domain, and adds
 * the ? to signify the start of the query parameters section.
 * @param kv_server_host_domain The full URL you want before the '?' for example
 * "https://kvserver.com/trusted-signals"".
 * @param url Output variable, url will be stored in this string. Note the
 * string will be cleared.
 */
void ClearAndMakeStartOfUrl(absl::string_view kv_server_host_domain,
                            std::string* url);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_UTIL_GENERATE_URL_H_
