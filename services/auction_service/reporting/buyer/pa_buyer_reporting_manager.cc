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
//  limitations under the License

#include "services/auction_service/reporting/buyer/pa_buyer_reporting_manager.h"

#include <utility>

#include "absl/status/statusor.h"
#include "rapidjson/document.h"
#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"
#include "services/auction_service/reporting/buyer/buyer_reporting_helper.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline std::vector<std::shared_ptr<std::string>> GetReportWinInput(
    std::shared_ptr<std::string> buyer_device_signals,
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const BuyerReportingDispatchRequestData& dispatch_request_data) {
  std::vector<std::shared_ptr<std::string>> input(kPAReportWinArgSize);
  input[PAReportWinArgIndex(PAReportWinArgs::kAuctionConfig)] =
      dispatch_request_data.auction_config;
  input[PAReportWinArgIndex(PAReportWinArgs::kPerBuyerSignals)] =
      std::make_shared<std::string>(dispatch_request_data.buyer_signals);
  input[PAReportWinArgIndex(PAReportWinArgs::kSignalsForWinner)] =
      std::make_shared<std::string>(dispatch_request_data.signals_for_winner);
  input[PAReportWinArgIndex(PAReportWinArgs::kBuyerReportingSignals)] =
      std::move(buyer_device_signals);
  // This is only added to prevent errors in the ReportWin ad script, and
  // will always be an empty object.
  input[PAReportWinArgIndex(PAReportWinArgs::kDirectFromSellerSignals)] =
      std::make_shared<std::string>("{}");
  input[PAReportWinArgIndex(PAReportWinArgs::kEnableLogging)] =
      std::make_shared<std::string>(
          dispatch_request_config.enable_adtech_code_logging ? "true"
                                                             : "false");
  PS_VLOG(kDispatch, dispatch_request_data.log_context)
      << "\n\nReportWin Input Args:" << "\nAuction Config:\n"
      << *(input[PAReportWinArgIndex(PAReportWinArgs::kAuctionConfig)])
      << "\nBuyer Reporting Signals:\n"
      << *(input[PAReportWinArgIndex(PAReportWinArgs::kBuyerReportingSignals)])
      << "\nSignals for winner:\n"
      << *(input[PAReportWinArgIndex(PAReportWinArgs::kSignalsForWinner)])
      << "\nBuyer reporting signals:\n"
      << *(input[PAReportWinArgIndex(PAReportWinArgs::kBuyerReportingSignals)])
      << "\nEnable logging:\n"
      << *(input[PAReportWinArgIndex(PAReportWinArgs::kEnableLogging)]);

  return input;
}

inline absl::StatusOr<DispatchRequest> GetReportWinDispatchRequest(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const BuyerReportingDispatchRequestData& request_data,
    std::shared_ptr<std::string> buyer_device_signals) {
  absl::StatusOr<std::string> version = GetBuyerReportWinVersion(
      request_data.buyer_origin, AuctionType::kProtectedAudience);
  if (!version.ok()) {
    // Todo(b/352227374) Add a metric when udf version generation fails.
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("No udf version found for winning buyer: ",
                                     request_data.buyer_origin));
  }
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest dispatch_request = {
      .id = request_data.winning_ad_render_url,
      .version_string = *version,
      .handler_name = kReportWinEntryFunction,
      .input = GetReportWinInput(std::move(buyer_device_signals),
                                 dispatch_request_config, request_data)};
  return dispatch_request;
}
}  // namespace

absl::Status PerformPAReportWin(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const BuyerReportingDispatchRequestData& request_data,
    rapidjson::Document& seller_device_signals,
    absl::AnyInvocable<
        void(const std::vector<absl::StatusOr<DispatchResponse>>&)>
        report_win_callback,
    V8DispatchClient& dispatcher) {
  std::shared_ptr<std::string> buyer_device_signals;
  PS_ASSIGN_OR_RETURN(
      buyer_device_signals,
      GenerateBuyerDeviceSignals(request_data, seller_device_signals));
  DispatchRequest dispatch_request;
  PS_ASSIGN_OR_RETURN(
      dispatch_request,
      GetReportWinDispatchRequest(dispatch_request_config, request_data,
                                  buyer_device_signals));
  dispatch_request.tags[kRomaTimeoutMs] =
      dispatch_request_config.roma_timeout_ms;
  std::vector<DispatchRequest> dispatch_requests = {
      std::move(dispatch_request)};
  return dispatcher.BatchExecute(dispatch_requests,
                                 std::move(report_win_callback));
}
}  // namespace privacy_sandbox::bidding_auction_servers
