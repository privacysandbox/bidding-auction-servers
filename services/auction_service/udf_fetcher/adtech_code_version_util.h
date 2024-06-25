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
#include "services/common/util/request_response_constants.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kProtectedAppSignalWrapperPrefix = "pas_";
inline constexpr absl::string_view kProtectedAuctionWrapperPrefix = "pa_";

// Returns the code version for reportWin() udf loaded for given
// buyer and platform
absl::StatusOr<std::string> GetBuyerReportWinVersion(
    absl::string_view buyer_origin, AuctionType auction_type);
// Returns Seller's default udf version
std::string GetDefaultSellerUdfVersion();
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_ADTECH_CODE_VERSION_UTIL_H_