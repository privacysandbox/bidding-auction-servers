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

#include "services/common/clients/http_kv_server/util/generate_url.h"

#include <algorithm>

#include <curl/curl.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace {
std::string EncodeParam(absl::string_view param) {
  char* encoded_chars = curl_easy_escape(NULL, param.data(), param.length());
  // Create copy to free up CURL allocated memory.
  std::string encoded_str(encoded_chars);
  curl_free(encoded_chars);
  return encoded_str;
}
}  // namespace

namespace privacy_sandbox::bidding_auction_servers {

void AddAmpersandIfNotFirstQueryParam(std::string* url) {
  if ((url->at(url->size() - 1) != '?') && url->at(url->size() - 1) != '&') {
    absl::StrAppend(url, "&");
  }
}

void AddListItemsAsQueryParamsToUrl(std::string* url, absl::string_view key,
                                    const UrlKeysSet& values,
                                    bool encode_params) {
  AddAmpersandIfNotFirstQueryParam(url);
  absl::StrAppend(url, key, "=");
  if (encode_params) {
    std::vector<std::string> encoded_values(values.size());
    std::transform(values.cbegin(), values.cend(), encoded_values.begin(),
                   [](absl::string_view str) { return EncodeParam(str); });
    absl::StrAppend(url, absl::StrJoin(encoded_values, ","));
  } else {
    absl::StrAppend(url, absl::StrJoin(values, ","));
  }
}

void ClearAndMakeStartOfUrl(absl::string_view kv_server_host_domain,
                            std::string* url) {
  *url = "";
  absl::StrAppend(url, kv_server_host_domain);
  absl::StrAppend(url, "?");
}

}  // namespace privacy_sandbox::bidding_auction_servers
