/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"

#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "services/auction_service/auction_constants.h"
#include "services/common/util/request_response_constants.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
absl::StatusOr<CURLU*> ParseUrl(absl::string_view url) {
  CURLU* url_handle = nullptr;

  // init
  url_handle = curl_url();
  if (!url_handle) {
    return absl::Status(absl::StatusCode::kInternal, "Curl URL init failed");
  }

  // parse
  CURLUcode uc = curl_url_set(url_handle, CURLUPART_URL, url.data(),
                              CURLU_DEFAULT_SCHEME | CURLU_GUESS_SCHEME);
  if (uc != CURLUE_OK) {
    if (url_handle) {
      curl_url_cleanup(url_handle);
    }
    return absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("Curl could not parse URL with error code: ", uc));
  }
  return url_handle;
}

}  // namespace

absl::StatusOr<std::string> StripQueryAndFragmentsFromUrl(
    absl::string_view url) {
  PS_ASSIGN_OR_RETURN(CURLU * url_handle, ParseUrl(url));
  char* path = nullptr;
  char* domain = nullptr;
  std::string full_url;
  CURLUcode uc = curl_url_get(url_handle, CURLUPART_HOST, &domain, 0);
  if (uc != CURLUE_OK) {
    curl_url_cleanup(url_handle);
    return absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("Curl could not get domain from URL with error code: ",
                     uc));
  }
  absl::StrAppend(&full_url, domain);
  curl_free(domain);

  uc = curl_url_get(url_handle, CURLUPART_PATH, &path, 0);
  if (uc != CURLUE_OK) {
    curl_url_cleanup(url_handle);
    return absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("Curl could not get domain from URL with error code: ",
                     uc));
  }
  absl::StrAppend(&full_url, path);
  curl_free(path);
  curl_url_cleanup(url_handle);
  return std::move(full_url);
}

absl::StatusOr<std::string> GetDomainFromUrl(absl::string_view url) {
  PS_ASSIGN_OR_RETURN(CURLU * url_handle, ParseUrl(url));
  // return domain/hostname
  char* domain;
  CURLUcode uc = curl_url_get(url_handle, CURLUPART_HOST, &domain, 0);
  if (uc != CURLUE_OK) {
    curl_url_cleanup(url_handle);
    return absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("Curl could not get domain from URL with error code: ",
                     uc));
  }
  std::string domain_str(domain);
  if (domain) {
    curl_free(domain);
  }
  curl_url_cleanup(url_handle);
  return std::move(domain_str);
}

absl::StatusOr<std::string> GetBuyerReportWinVersion(
    absl::string_view buyer_url, AuctionType auction_type) {
  PS_ASSIGN_OR_RETURN(std::string domain, GetDomainFromUrl(buyer_url));
  switch (auction_type) {
    case AuctionType::kProtectedAppSignals:
      return absl::StrCat(kProtectedAppSignalWrapperPrefix, domain);
    case AuctionType::kProtectedAudience:
      return absl::StrCat(kProtectedAuctionWrapperPrefix, domain);
    default:
      return absl::Status(
          absl::StatusCode::kInvalidArgument,
          "Error generating buyer reportWin version. Unknown auction type.");
  }
}

std::string GetDefaultSellerUdfVersion() { return kScoreAdBlobVersion; }

}  // namespace privacy_sandbox::bidding_auction_servers
