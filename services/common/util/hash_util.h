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

#ifndef SERVICES_COMMON_UTIL_HASH_UTIL_H_
#define SERVICES_COMMON_UTIL_HASH_UTIL_H_

#include <string>

#include "absl/strings/string_view.h"
#include "services/common/util/hash_util_interface.h"
#include "url/gurl.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kKAnonKeyForAdComponentBidPrefix = "ComponentBid\n";
constexpr absl::string_view kKAnonKeyForAdBidPrefix = "AdBid\n";
constexpr absl::string_view
    kKAnonKeyForAdNameReportingSelectedBuyerAndSellerIdPrefix =
        "SelectedBuyerAndSellerReportId\n";
constexpr absl::string_view kKAnonKeyForAdNameReportingBuyerAndSellerIdPrefix =
    "BuyerAndSellerReportId\n";
constexpr absl::string_view kKAnonKeyForAdNameReportingBuyerReportIdPrefix =
    "BuyerReportId\n";
constexpr absl::string_view kKAnonKeyForAdNameReportingNamePrefix =
    "NameReport\n";
constexpr int kReportingIDSizeBytes = 4;

// Wrapper around OpenSSL to compute the SHA256 hash of a given input string.
// It converts the resulting binary hash into a hexadecimal string.
std::string ComputeSHA256(absl::string_view data, bool return_hex = true);

// Canonicalize valid URLs.
inline std::string CanonicalizeURL(absl::string_view url) {
  return GURL(gurl_base::StringPiece(url.data())).spec();
}

// Creates a string key for ad render URL to be hashed.
std::string PlainTextKAnonKeyForAdRenderURL(absl::string_view owner,
                                            absl::string_view bidding_url,
                                            absl::string_view render_url);

// Creates a string key for component ad render URL to be hashed.
std::string PlainTextKAnonKeyForAdComponentRenderURL(
    absl::string_view component_render_url);

// Creates a string key for reporting ID to be hashed.
std::string PlainTextKAnonKeyForReportingID(
    absl::string_view owner, absl::string_view ig_name,
    absl::string_view bidding_url, absl::string_view render_url,
    const KAnonKeyReportingIDParam& reporting_ids);

class HashUtil : public HashUtilInterface {
 public:
  // Computes the key hash of ad render URL.
  std::string HashedKAnonKeyForAdRenderURL(
      absl::string_view owner, absl::string_view bidding_url,
      absl::string_view render_url) override;

  // Computes the key hash of ad component render URL.
  std::string HashedKAnonKeyForAdComponentRenderURL(
      absl::string_view component_render_url) override;

  // Computes the key hash of reporting ID.
  std::string HashedKAnonKeyForReportingID(
      absl::string_view owner, absl::string_view ig_name,
      absl::string_view bidding_url, absl::string_view render_url,
      const KAnonKeyReportingIDParam& reporting_ids) override;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_HASH_UTIL_H_
