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

#ifndef SERVICES_ADTECH_CODE_VERSION_UTIL_H_
#define SERVICES_ADTECH_CODE_VERSION_UTIL_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "curl/curl.h"
#include "services/common/util/request_response_constants.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kProtectedAppSignalWrapperPrefix = "pas_";
inline constexpr absl::string_view kProtectedAuctionWrapperPrefix = "pa_";

// Parses a URL(protocol://domain/path?query#fragment)
// based on https://datatracker.ietf.org/doc/html/rfc3986
// and returns the domain. For eg -
// Returns abc.com from https://abc.com/1/2?3=4#5
absl::StatusOr<std::string> GetDomainFromUrl(absl::string_view url);

// Parses a URL(protocol://domain/path?query#fragment)
// based on https://datatracker.ietf.org/doc/html/rfc3986
// and returns the domain/path. For eg -
// Returns abc.com/1/2 from https://abc.com/1/2?3=4#5
absl::StatusOr<std::string> StripQueryAndFragmentsFromUrl(
    absl::string_view url);

// Returns the code version for reportWin() udf loaded for given
// buyer and platform. This same function should be used for loading
// and retreiving the code version for reportWin() udf.
// This works as a comparison check - the lookup will only succeed if
// the buyer domain is the same as the reportWin() udf url domain.
absl::StatusOr<std::string> GetBuyerReportWinVersion(
    absl::string_view buyer_url, AuctionType auction_type);

// Returns Seller's default udf version
std::string GetDefaultSellerUdfVersion();
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_ADTECH_CODE_VERSION_UTIL_H_
