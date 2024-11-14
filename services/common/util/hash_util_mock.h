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

#ifndef SERVICES_COMMON_UTIL_HASH_UTIL_MOCK_H_
#define SERVICES_COMMON_UTIL_HASH_UTIL_MOCK_H_

#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "services/common/util/hash_util_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class HashUtilMock : public HashUtilInterface {
 public:
  MOCK_METHOD(std::string, HashedKAnonKeyForAdRenderURL,
              (absl::string_view owner, absl::string_view bidding_url,
               absl::string_view render_url),
              (override));

  MOCK_METHOD(std::string, HashedKAnonKeyForAdComponentRenderURL,
              (absl::string_view component_render_url), (override));

  MOCK_METHOD(std::string, HashedKAnonKeyForReportingID,
              (absl::string_view owner, absl::string_view ig_name,
               absl::string_view bidding_url, absl::string_view render_url,
               const KAnonKeyReportingIDParam& reporting_ids),
              (override));
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_HASH_UTIL_MOCK_H_
