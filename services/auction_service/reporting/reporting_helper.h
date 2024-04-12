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
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/util/post_auction_signals.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kReportResultResponse[] = "reportResultResponse";
inline constexpr char kReportResultUrl[] = "reportResultUrl";
inline constexpr char kSendReportToInvoked[] = "sendReportToInvoked";
inline constexpr char kRegisterAdBeaconInvoked[] = "registerAdBeaconInvoked";
inline constexpr char kTopWindowHostname[] = "topWindowHostname";
inline constexpr char kRenderURL[] = "renderURL";
inline constexpr char kRenderUrl[] = "renderUrl";
inline constexpr char kDesirability[] = "desirability";
inline constexpr char kHighestScoringOtherBid[] = "highestScoringOtherBid";
inline constexpr char kSellerLogs[] = "sellerLogs";
inline constexpr char kSellerErrors[] = "sellerErrors";
inline constexpr char kSellerWarnings[] = "sellerWarnings";
inline constexpr char kBuyerLogs[] = "buyerLogs";
inline constexpr char kBuyerErrors[] = "buyerErrors";
inline constexpr char kBuyerWarnings[] = "buyerWarnings";
inline constexpr int kReportingArgSize = 5;
inline constexpr int kReportingArgSizeWithProtectedAppSignals = 6;
inline constexpr char kReportingDispatchHandlerFunctionName[] =
    "reportingEntryFunction";
inline constexpr char kReportingProtectedAppSignalsFunctionName[] =
    "reportingEntryFunctionProtectedAppSignals";
inline constexpr char kDispatchRequestVersion[] = "v1";
inline constexpr char kTopLevelSellerTag[] = "topLevelSeller";
inline constexpr char kComponentSeller[] = "componentSeller";
inline constexpr char kEnableReportWinUrlGeneration[] =
    "enableReportWinUrlGeneration";
inline constexpr char kEnableProtectedAppSignals[] =
    "enableProtectedAppSignals";
inline constexpr char kModelingSignalsTag[] = "modelingSignals";
inline constexpr char kModifiedBid[] = "modifiedBid";
inline constexpr char kReportWinResponse[] = "reportWinResponse";
inline constexpr char kReportWinUrl[] = "reportWinUrl";
inline constexpr char kBuyerSignals[] = "perBuyerSignals";
inline constexpr char kBuyerOriginTag[] = "buyerOrigin";
inline constexpr char kSellerTag[] = "seller";
inline constexpr char kAdCostTag[] = "adCost";
inline constexpr char kMadeHighestScoringOtherBid[] =
    "madeHighestScoringOtherBid";
inline constexpr char kInteractionReportingUrlsWrapperResponse[] =
    "interactionReportingUrls";

enum class ReportingArgs : int {
  kAuctionConfig = 0,
  kSellerReportingSignals,
  kDirectFromSellerSignals,
  kEnableAdTechCodeLogging,
  kBuyerReportingMetadata,
  kEgressFeatures
};

inline constexpr int ReportingArgIndex(const ReportingArgs& arg) {
  return static_cast<std::underlying_type_t<ReportingArgs>>(arg);
}

struct BuyerReportingMetadata {
  std::string buyer_signals;
  std::optional<int> join_count;
  std::optional<long> recency;
  std::optional<int> modeling_signals;
  std::string seller;
  std::string interest_group_name;
  double ad_cost;
};

struct ComponentReportingMetadata {
  std::string top_level_seller;
  std::string component_seller;
  float modified_bid;
};

// Config flags used to construct the inputs for inputs to dispatch
// reporting function execution.
struct ReportingDispatchRequestConfig {
  bool enable_report_win_url_generation = false;
  bool enable_protected_app_signals = false;
  bool enable_report_win_input_noising = false;
  bool enable_adtech_code_logging = false;
};

// Data required to build the inputs required to dispatch
// reporting function execution.
struct ReportingDispatchRequestData {
  std::string handler_name;
  std::shared_ptr<std::string> auction_config;
  PostAuctionSignals post_auction_signals;
  std::string_view publisher_hostname;
  server_common::log::ContextImpl& log_context;
  BuyerReportingMetadata buyer_reporting_metadata;
  ComponentReportingMetadata component_reporting_metadata;
  absl::string_view egress_features;
};

// Bid metadata passed as input to reportResult and reportWin
struct SellerReportingMetadata {
  std::string top_window_hostname;
  std::string top_level_seller;
  std::string component_seller;
  std::string interest_group_owner;
  std::string render_url;
  float bid;
  float desirability;
  float modified_bid;
  float highest_scoring_other_bid;
};

inline const std::string kDefaultBuyerReportingMetadata = absl::StrFormat(
    R"JSON(
        {
          "%s" : false
        }
        )JSON",
    kEnableReportWinUrlGeneration);

inline const std::string kDefaultComponentReportingMetadata = absl::StrFormat(
    R"JSON(
        {
          "%s" : ""
        }
        )JSON",
    kTopLevelSellerTag);

// Parses json object returned from execution of reportingEntryFunction in Roma
// and returns the ReportingResponse.
absl::StatusOr<ReportingResponse> ParseAndGetReportingResponse(
    bool enable_adtech_code_logging, const std::string& response);

// Creates the input arguments required for executing reportingEntryFunction in
// Roma.
std::vector<std::shared_ptr<std::string>> GetReportingInput(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const ReportingDispatchRequestData& dispatch_request_data);

// Creates the DispatchRequest for calling reportingEntryFunction in Roma
DispatchRequest GetReportingDispatchRequest(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const ReportingDispatchRequestData& dispatch_request_data);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_H_
