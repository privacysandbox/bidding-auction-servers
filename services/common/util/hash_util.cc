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

#include "services/common/util/hash_util.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <openssl/sha.h>

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
// TODO (b/376279776): Replace this function with base::U32ToBigEndian once GURL
// is integrated with B&A
std::string LittleEndianU32ToBigEndianString(uint32_t little_endian_num) {
  std::vector<uint8_t> big_endian_bytes;
  for (int i = kReportingIDSizeBytes - 1; i >= 0; --i) {
    big_endian_bytes.push_back(
        static_cast<uint8_t>(little_endian_num >> (i * 8)));
  }
  return std::string(big_endian_bytes.begin(), big_endian_bytes.end());
}

void AppendReportingIdForSelectedReportingKeyKAnonKey(
    std::optional<std::string> reporting_id, std::string& k_anon_key) {
  if (!reporting_id) {
    absl::StrAppend(&k_anon_key, "\n",
                    absl::string_view("\x00\x00\x00\x00\x00", 5));
    return;
  }
  absl::StrAppend(&k_anon_key, "\n", absl::string_view("\x01", 1),
                  LittleEndianU32ToBigEndianString(reporting_id->size()),
                  *reporting_id);
  return;
}
}  // namespace

std::string ComputeSHA256(absl::string_view data, bool return_hex) {
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, data.data(), data.size());
  SHA256_Final(hash, &sha256);
  if (!return_hex) {
    return std::string(std::begin(hash), std::end(hash));
  }

  constexpr ptrdiff_t kTwoDigestLength = SHA256_DIGEST_LENGTH * 2;
  char output_buf[kTwoDigestLength + 1];
  output_buf[kTwoDigestLength] = 0;
  for (long i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
    ptrdiff_t i_times_2 = i * 2;
    snprintf(output_buf + i_times_2, sizeof(output_buf) - i_times_2, "%02x",
             hash[i]);
  }
  return std::string(output_buf);
}

inline std::string HashAndMayLogResult(absl::string_view plain_key) {
  std::string result = ComputeSHA256(plain_key, /*return_hex=*/false);
  PS_VLOG(5) << " " << __func__ << " plain key: " << plain_key
             << "\nhashed to: " << absl::BytesToHexString(result);
  return result;
}

std::string PlainTextKAnonKeyForAdRenderURL(absl::string_view owner,
                                            absl::string_view bidding_url,
                                            absl::string_view render_url) {
  return absl::StrCat(kKAnonKeyForAdBidPrefix, CanonicalizeURL(owner), "\n",
                      CanonicalizeURL(bidding_url), "\n",
                      CanonicalizeURL(render_url));
}

std::string PlainTextKAnonKeyForAdComponentRenderURL(
    absl::string_view component_render_url) {
  return absl::StrCat(kKAnonKeyForAdComponentBidPrefix,
                      CanonicalizeURL(component_render_url));
}

std::string PlainTextKAnonKeyForReportingID(
    absl::string_view owner, absl::string_view ig_name,
    absl::string_view bidding_url, absl::string_view render_url,
    const KAnonKeyReportingIDParam& reporting_ids) {
  std::optional<std::string> selected_id =
      reporting_ids.selected_buyer_and_seller_reporting_id;
  std::optional<std::string> buyer_seller_id =
      reporting_ids.buyer_and_seller_reporting_id;
  std::optional<std::string> buyer_id = reporting_ids.buyer_reporting_id;
  std::string middle =
      absl::StrCat(CanonicalizeURL(owner), "\n", CanonicalizeURL(bidding_url),
                   "\n", CanonicalizeURL(render_url));
  if (selected_id) {
    std::string k_anon_key = absl::StrCat(
        kKAnonKeyForAdNameReportingSelectedBuyerAndSellerIdPrefix, middle);
    AppendReportingIdForSelectedReportingKeyKAnonKey(selected_id, k_anon_key);
    AppendReportingIdForSelectedReportingKeyKAnonKey(buyer_seller_id,
                                                     k_anon_key);
    AppendReportingIdForSelectedReportingKeyKAnonKey(buyer_id, k_anon_key);
    return k_anon_key;
  }
  if (buyer_seller_id) {
    return absl::StrCat(kKAnonKeyForAdNameReportingBuyerAndSellerIdPrefix,
                        middle, "\n", *buyer_seller_id);
  }
  if (buyer_id) {
    return absl::StrCat(kKAnonKeyForAdNameReportingBuyerReportIdPrefix, middle,
                        "\n", *buyer_id);
  }
  return absl::StrCat(kKAnonKeyForAdNameReportingNamePrefix, middle, "\n",
                      ig_name);
}

std::string HashUtil::HashedKAnonKeyForAdRenderURL(
    absl::string_view owner, absl::string_view bidding_url,
    absl::string_view render_url) {
  return HashAndMayLogResult(
      PlainTextKAnonKeyForAdRenderURL(owner, bidding_url, render_url));
}

std::string HashUtil::HashedKAnonKeyForAdComponentRenderURL(
    absl::string_view component_render_url) {
  return HashAndMayLogResult(
      PlainTextKAnonKeyForAdComponentRenderURL(component_render_url));
}

std::string HashUtil::HashedKAnonKeyForReportingID(
    absl::string_view owner, absl::string_view ig_name,
    absl::string_view bidding_url, absl::string_view render_url,
    const KAnonKeyReportingIDParam& reporting_ids) {
  return HashAndMayLogResult(PlainTextKAnonKeyForReportingID(
      owner, ig_name, bidding_url, render_url, reporting_ids));
}

}  // namespace privacy_sandbox::bidding_auction_servers
