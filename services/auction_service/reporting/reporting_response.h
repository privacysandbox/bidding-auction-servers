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
#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
// Response received from execution of reportResult() in Roma.
struct ReportResultResponse {
  std::string report_result_url;
  absl::flat_hash_map<std::string, std::string> interaction_reporting_urls;
  bool send_report_to_invoked;
  bool register_ad_beacon_invoked;
  std::string signals_for_winner;
  PrivateAggregateReportingResponse pagg_response;
};

struct ReportWinResponse {
  std::string report_win_url;
  absl::flat_hash_map<std::string, std::string> interaction_reporting_urls;
  bool send_report_to_invoked;
  bool register_ad_beacon_invoked;
  PrivateAggregateReportingResponse pagg_response;
};

struct ReportingResponseLogs {
  std::vector<std::string> logs;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

// Response received from execution of reportingEntryFunction() in Roma.
struct ReportingResponse {
  ReportResultResponse report_result_response;
  std::vector<std::string> seller_logs;
  std::vector<std::string> seller_error_logs;
  std::vector<std::string> seller_warning_logs;
  ReportWinResponse report_win_response;
  std::vector<std::string> buyer_logs;
  std::vector<std::string> buyer_error_logs;
  std::vector<std::string> buyer_warning_logs;
};
}  //  namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_AUCTION_SERVICE_REPORTING_RESPONSE_H_
