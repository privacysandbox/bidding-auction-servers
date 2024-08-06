//  Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_PUBLIC_KEY_URL_ALLOWLIST_H_
#define SERVICES_COMMON_PUBLIC_KEY_URL_ALLOWLIST_H_

#include <string>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Endpoint not allowlisted error message prefix.
inline constexpr absl::string_view kEndpointNotAllowlisted =
    "Endpoint not in public key url allowlist: ";

// clang-format off
inline constexpr absl::string_view kGCPProdPublicKeyEndpoint =
    "https://publickeyservice.pa.gcp.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys";  // NOLINT(whitespace/line_length)
inline constexpr absl::string_view kAWSProdPublicKeyEndpoint =
    "https://publickeyservice.pa.aws.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys";  // NOLINT(whitespace/line_length)
// clang-format on

// Checks if the url is in the public key allowlist.
//
// All services must only fetch their public keys from a sanctioned public key
// url, although the operator is free to choose from the public key urls that
// make sense for their environment.
// If not running a prod build, allow any url.
inline bool IsAllowedPublicKeyUrl(absl::string_view url, bool is_prod_build) {
  static const absl::NoDestructor<absl::flat_hash_set<absl::string_view>>
      kPublicKeyUrlAllowlist({
          // clang-format off
            kGCPProdPublicKeyEndpoint,
            kAWSProdPublicKeyEndpoint,
          // clang-format on
      });

  return kPublicKeyUrlAllowlist->contains(url) || !is_prod_build;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_PUBLIC_KEY_URL_ALLOWLIST_H_
