//  Copyright 2025 Google LLC
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

#include "services/common/clients/http/curl_request_data.h"

#include <utility>

namespace privacy_sandbox::bidding_auction_servers {

CurlRequestData::CurlRequestData(const std::vector<std::string>& headers,
                                 OnDoneFetchUrlWithMetadata on_done,
                                 std::vector<std::string> response_header_keys,
                                 bool include_redirect_url)
    : req_handle(curl_easy_init()),
      done_callback(std::move(on_done)),
      response_headers(std::move(response_header_keys)),
      include_redirect_url(include_redirect_url) {
  for (const auto& header : headers) {
    headers_list_ptr = curl_slist_append(headers_list_ptr, header.c_str());
  }
}

CurlRequestData::~CurlRequestData() {
  curl_slist_free_all(headers_list_ptr);
  curl_easy_cleanup(req_handle);
}

}  // namespace privacy_sandbox::bidding_auction_servers
