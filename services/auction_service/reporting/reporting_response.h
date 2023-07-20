//  Copyright 2023 Google LLC
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
#ifndef SERVICES_AUCTION_SERVICE_REPORTING_RESPONSE_H_
#define SERVICES_AUCTION_SERVICE_REPORTING_RESPONSE_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace privacy_sandbox::bidding_auction_servers {
// Response received from execution of reportResult() in Roma.
struct ReportResultResponse {
  std::string report_result_url;
  std::string signals_for_winner;
  absl::flat_hash_map<std::string, std::string> interaction_reporting_urls;
  bool send_report_to_invoked;
  bool register_ad_beacon_invoked;
};

// Response received from execution of reportingEntryFunction() in Roma.
struct ReportingResponse {
  ReportResultResponse report_result_response;
  std::vector<std::string> seller_logs;
};
}  //  namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_AUCTION_SERVICE_REPORTING_RESPONSE_H_
