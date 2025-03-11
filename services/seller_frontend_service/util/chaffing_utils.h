/*
 * Copyright 2025 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_

#include <vector>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

struct InvokedBuyers {
  std::vector<absl::string_view> chaff_buyer_names;
  std::vector<absl::string_view> non_chaff_buyer_names;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_
