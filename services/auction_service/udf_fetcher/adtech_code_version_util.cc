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

#include "absl/strings/str_cat.h"
#include "services/auction_service/auction_constants.h"
#include "services/common/util/request_response_constants.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::string> GetBuyerReportWinVersion(
    absl::string_view buyer_origin, AuctionType auction_type) {
  switch (auction_type) {
    case AuctionType::kProtectedAppSignals:
      return absl::StrCat(kProtectedAppSignalWrapperPrefix, buyer_origin);
    case AuctionType::kProtectedAudience:
      return absl::StrCat(kProtectedAuctionWrapperPrefix, buyer_origin);
    default:
      return absl::Status(
          absl::StatusCode::kInvalidArgument,
          "Error generating buyer reportWin version. Unknown auction type.");
  }
}

std::string GetDefaultSellerUdfVersion() { return kScoreAdBlobVersion; }

}  // namespace privacy_sandbox::bidding_auction_servers
