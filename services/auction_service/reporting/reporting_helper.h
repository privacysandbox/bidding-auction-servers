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
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/post_auction_signals.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kReportResultResponse[] = "reportResultResponse";
inline constexpr char kResponse[] = "response";
inline constexpr char kSignalsForWinner[] = "signalsForWinner";
inline constexpr char kReportResultUrl[] = "reportResultUrl";
inline constexpr char kSendReportToInvoked[] = "sendReportToInvoked";
inline constexpr char kRegisterAdBeaconInvoked[] = "registerAdBeaconInvoked";
inline constexpr char kTopWindowHostnameTag[] = "topWindowHostname";
inline constexpr char kRenderURLTag[] = "renderURL";
inline constexpr char kRenderUrlTag[] = "renderUrl";
inline constexpr char kDesirabilityTag[] = "desirability";
inline constexpr char kHighestScoringOtherBidTag[] = "highestScoringOtherBid";
inline constexpr char kSellerLogs[] = "sellerLogs";
inline constexpr char kSellerErrors[] = "sellerErrors";
inline constexpr char kSellerWarnings[] = "sellerWarnings";
inline constexpr char kReportingUdfLogs[] = "logs";
inline constexpr char kReportingUdfErrors[] = "errors";
inline constexpr char kReportingUdfWarnings[] = "warnings";
inline constexpr char kBuyerLogs[] = "buyerLogs";
inline constexpr char kBuyerErrors[] = "buyerErrors";
inline constexpr char kBuyerWarnings[] = "buyerWarnings";
inline constexpr int kReportingArgSize = 5;
static constexpr char kRomaTimeoutMs[] = "TimeoutMs";
inline constexpr int kReportingArgSizeWithProtectedAppSignals = 7;
inline constexpr char kReportingDispatchHandlerFunctionName[] =
    "reportingEntryFunction";
inline constexpr char kReportingProtectedAppSignalsFunctionName[] =
    "reportingEntryFunctionProtectedAppSignals";
inline constexpr char kTopLevelSellerTag[] = "topLevelSeller";
inline constexpr char kWinningBidCurrencyTag[] = "bidCurrency";
inline constexpr char kHighestScoringOtherBidCurrencyTag[] =
    "highestScoringOtherBidCurrency";
inline constexpr char kModifiedBidCurrencyTag[] = "modifiedBidCurrency";
inline constexpr char kComponentSeller[] = "componentSeller";
inline constexpr char kEnableReportWinUrlGeneration[] =
    "enableReportWinUrlGeneration";
inline constexpr char kEnableProtectedAppSignals[] =
    "enableProtectedAppSignals";
inline constexpr char kModelingSignalsTag[] = "modelingSignals";
inline constexpr char kReportWinResponse[] = "reportWinResponse";
inline constexpr char kReportWinUrl[] = "reportWinUrl";
inline constexpr char kBuyerSignals[] = "perBuyerSignals";
inline constexpr char kBuyerOriginTag[] = "buyerOrigin";
inline constexpr char kSellerTag[] = "seller";
inline constexpr char kAdCostTag[] = "adCost";
inline constexpr char kBuyerReportingIdTag[] = "buyerReportingId";
inline constexpr char kMadeHighestScoringOtherBid[] =
    "madeHighestScoringOtherBid";
inline constexpr char kInteractionReportingUrlsWrapperResponse[] =
    "interactionReportingUrls";
inline constexpr char kBidTag[] = "bid";
inline constexpr char kInterestGroupOwnerTag[] = "interestGroupOwner";
inline constexpr char kInterestGroupNameTag[] = "interestGroupName";
inline constexpr char kJoinCountTag[] = "joinCount";
inline constexpr char kRecencyTag[] = "recency";
constexpr int kStochasticalRoundingBits = 8;

// [[deprecated("DEPRECATED. Please use
// SellerReportingDispatchRequestData or BuyerReportingDispatchRequestData
// instead")]]
enum class ReportingArgs : int {
  kAuctionConfig = 0,
  kSellerReportingSignals,
  kDirectFromSellerSignals,
  kEnableAdTechCodeLogging,
  kBuyerReportingMetadata,
  kEgressPayload,
  kTemporaryEgressPayload,
};

inline constexpr int ReportingArgIndex(const ReportingArgs& arg) {
  return static_cast<std::underlying_type_t<ReportingArgs>>(arg);
}

// [[deprecated("DEPRECATED. Please use
// BuyerReportingDispatchRequestData instead")]]
struct BuyerReportingMetadata {
  std::string buyer_signals;
  std::optional<int> join_count;
  std::optional<long> recency;
  std::optional<int> modeling_signals;
  std::string seller;
  std::string interest_group_name;
  double ad_cost;
  std::string buyer_reporting_id;
};

struct ComponentReportingMetadata {
  std::string top_level_seller;
  std::string component_seller;
  // Todo(b/353771714): Remove modified_bid_currency
  std::string modified_bid_currency;
  float modified_bid;
};

// Config flags used to construct the inputs for inputs to dispatch
// reporting function execution.
struct ReportingDispatchRequestConfig {
  bool enable_report_win_url_generation = false;
  bool enable_protected_app_signals = false;
  bool enable_report_win_input_noising = false;
  bool enable_adtech_code_logging = false;
  std::string roma_timeout_ms;
};

// [[deprecated("DEPRECATED. Please use
// SellerReportingDispatchRequestData or BuyerReportingDispatchRequestData
// instead")]]
struct ReportingDispatchRequestData {
  std::string handler_name;
  std::shared_ptr<std::string> auction_config;
  PostAuctionSignals post_auction_signals;
  std::string_view publisher_hostname;
  RequestLogContext& log_context;
  BuyerReportingMetadata buyer_reporting_metadata;
  ComponentReportingMetadata component_reporting_metadata;
  absl::string_view egress_payload;
  absl::string_view temporary_egress_payload;
};

// [[deprecated("DEPRECATED. Please use
// SellerReportingDispatchRequestData instead")]]
struct SellerReportingMetadata {
  std::string top_window_hostname;
  std::string top_level_seller;
  std::string component_seller;
  std::string interest_group_owner;
  std::string render_url;
  float bid;
  std::string bid_currency;
  float desirability;
  float modified_bid;
  std::string modified_bid_currency;
  float highest_scoring_other_bid;
  std::string highest_scoring_other_bid_currency;
};

// Data required to build the inputs for
// reportResult() execution.
struct SellerReportingDispatchRequestData {
  std::shared_ptr<std::string> auction_config;
  PostAuctionSignals& post_auction_signals;
  std::string_view publisher_hostname;
  ComponentReportingMetadata component_reporting_metadata;
  RequestLogContext& log_context;
};

struct BuyerReportingDispatchRequestData {
  std::shared_ptr<std::string> auction_config;
  std::string buyer_signals;
  std::optional<int> join_count;
  std::optional<long> recency;
  std::optional<int> modeling_signals;
  std::string seller;
  std::string interest_group_name;
  double ad_cost;
  std::optional<std::string> buyer_reporting_id;
  bool made_highest_scoring_other_bid;
  RequestLogContext& log_context;
  std::string buyer_origin;
  std::string signals_for_winner = "null";
  std::string winning_ad_render_url;
  std::optional<absl::string_view> egress_payload;
  std::optional<absl::string_view> temporary_unlimited_egress_payload;
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
[[deprecated]] absl::StatusOr<ReportingResponse> ParseAndGetReportingResponse(
    bool enable_adtech_code_logging, const std::string& response);

// Creates the input arguments required for executing reportingEntryFunction in
// Roma.
[[deprecated]] std::vector<std::shared_ptr<std::string>> GetReportingInput(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const ReportingDispatchRequestData& dispatch_request_data);

// Creates the DispatchRequest for calling reportingEntryFunction in Roma
[[deprecated]] DispatchRequest GetReportingDispatchRequest(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const ReportingDispatchRequestData& dispatch_request_data);

// Stochastically rounds floating point value to 8 bit mantissa and 8
// bit exponent. If there is an error while generating the rounded
// number, -1 will be returned.
double GetEightBitRoundedValue(double value);
// Handles AdTech logs exported from Roma.
// - Parses document for "logs", "errors" and "warnings" keys and gets array
// values for each of them.
// - Logs the parsed strings along with a message that includes log_type,
// udf_name using PS_VLOG and log_context.
void HandleUdfLogs(const rapidjson::Document& document,
                   const std::string& log_type, absl::string_view udf_name,
                   RequestLogContext& log_context);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_H_
