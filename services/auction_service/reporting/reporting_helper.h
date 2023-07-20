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

#ifndef SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_H_
#define SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kReportResultResponse[] = "reportResultResponse";
inline constexpr char kReportResultUrl[] = "reportResultUrl";
inline constexpr char kSignalsForWinner[] = "signalsForWinner";
inline constexpr char kSendReportToInvoked[] = "sendReportToInvoked";
inline constexpr char kRegisterAdBeaconInvoked[] = "registerAdBeaconInvoked";
inline constexpr char kInteractionReportingUrls[] = "interactionReportingUrls";
inline constexpr char kTopWindowHostname[] = "topWindowHostname";
inline constexpr char kInterestGroupOwner[] = "interestGroupOwner";
inline constexpr char kRenderURL[] = "renderURL";
inline constexpr char kBid[] = "bid";
inline constexpr char kDesirability[] = "desirability";
inline constexpr char kHighestScoringOtherBid[] = "highestScoringOtherBid";
inline constexpr char kSellerLogs[] = "sellerLogs";
inline constexpr int kReportingArgSize = 4;
inline constexpr char kReportingDispatchHandlerFunctionName[] =
    "reportingEntryFunction";
inline constexpr int kDispatchRequestVersionNumber = 1.0;

enum class ReportingArgs : int {
  kAuctionConfig = 0,
  kSellerReportingSignals,
  kDirectFromSellerSignals,
  kEnableAdTechCodeLogging,
};

inline constexpr int ReportingArgIndex(const ReportingArgs& arg) {
  return static_cast<std::underlying_type_t<ReportingArgs>>(arg);
}

// Parses json object returned from execution of reportingEntryFunction in Roma
// and returns the ReportingResponse.
absl::StatusOr<ReportingResponse> ParseAndGetReportingResponse(
    bool enable_adtech_code_logging, const std::string& response);

// Creates the input arguments required for executing reportingEntryFunction in
// Roma.
std::vector<std::shared_ptr<std::string>> GetReportingInput(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::string& publisher_hostname, bool enable_adtech_code_logging,
    std::shared_ptr<std::string> auction_config, const ContextLogger& logger);

// Creates the DispatchRequest for calling reportingEntryFunction in Roma
DispatchRequest GetReportingDispatchRequest(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const std::string& publisher_hostname, bool enable_adtech_code_logging,
    std::shared_ptr<std::string> auction_config, const ContextLogger& logger);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_H_
