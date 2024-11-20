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

#ifndef SERVICES_COMMON_UTIL_HASH_UTIL_INTERFACE_H_
#define SERVICES_COMMON_UTIL_HASH_UTIL_INTERFACE_H_

#include <optional>
#include <string>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// This struct groups all reporting IDs needed for hash calculation.
struct KAnonKeyReportingIDParam {
  std::optional<std::string> buyer_reporting_id = std::nullopt;
  std::optional<std::string> buyer_and_seller_reporting_id = std::nullopt;
  std::optional<std::string> selected_buyer_and_seller_reporting_id =
      std::nullopt;
};

// TODO (b/376309224): Remove this interface once all the implementation is
// done, since it's only for testing purposes.
class HashUtilInterface {
 public:
  virtual ~HashUtilInterface() = default;

  // Computes the key hash of ad render URL.
  virtual std::string HashedKAnonKeyForAdRenderURL(
      absl::string_view owner, absl::string_view bidding_url,
      absl::string_view render_url) = 0;

  // Computes the key hash of ad component render URL.
  virtual std::string HashedKAnonKeyForAdComponentRenderURL(
      absl::string_view component_render_url) = 0;

  // Computes the key hash of reporting ID.
  virtual std::string HashedKAnonKeyForReportingID(
      absl::string_view owner, absl::string_view ig_name,
      absl::string_view bidding_url, absl::string_view render_url,
      const KAnonKeyReportingIDParam& reporting_ids) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_HASH_UTIL_INTERFACE_H_
