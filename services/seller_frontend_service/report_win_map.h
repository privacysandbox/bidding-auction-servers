/*
 * Copyright 2024: Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_REPORT_WIN_MAP_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_REPORT_WIN_MAP_H_

#include <string>

#include "absl/container/flat_hash_map.h"

namespace privacy_sandbox::bidding_auction_servers {

// Aggregate of report win URLs for buyers.
struct ReportWinMap {
  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  absl::flat_hash_map<std::string, std::string>
      protected_app_signals_buyer_report_win_js_urls;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_REPORT_WIN_MAP_H_
