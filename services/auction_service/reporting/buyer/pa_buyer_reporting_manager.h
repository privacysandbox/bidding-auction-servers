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

#ifndef SERVICES_AUCTION_SERVICE_REPORTING_BUYER_REPORTING_MANAGER_H_
#define SERVICES_AUCTION_SERVICE_REPORTING_BUYER_REPORTING_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "rapidjson/document.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"

namespace privacy_sandbox::bidding_auction_servers {
inline constexpr int kPAReportWinArgSize = 6;
enum class PAReportWinArgs : int {
  kAuctionConfig,
  kPerBuyerSignals,
  kSignalsForWinner,
  kBuyerReportingSignals,
  kDirectFromSellerSignals,
  kEnableLogging
};

inline constexpr int PAReportWinArgIndex(PAReportWinArgs arg) {
  return static_cast<std::underlying_type_t<PAReportWinArgs>>(arg);
}

// Generates the DispatchRequest and invokes reportWin() udf with
// report_win_callback function for Protected Audience auctions
absl::Status PerformPAReportWin(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const BuyerReportingDispatchRequestData& request_data,
    rapidjson::Document& seller_device_signals,
    absl::AnyInvocable<
        void(const std::vector<absl::StatusOr<DispatchResponse>>&)>
        report_win_callback,
    V8DispatchClient& dispatcher);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_BUYER_REPORTING_MANAGER_H_
