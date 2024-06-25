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

#include "services/auction_service/reporting/seller/seller_reporting_manager.h"

#include <utility>

#include "absl/status/statusor.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline std::vector<std::shared_ptr<std::string>> GetReportResultInput(
    const std::string& seller_device_signals,
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const SellerReportingDispatchRequestData& dispatch_request_data) {
  std::vector<std::shared_ptr<std::string>> input(
      kReportResultArgSize);  // ReportResultArgs size

  input[ReportResultArgIndex(ReportResultArgs::kAuctionConfig)] =
      dispatch_request_data.auction_config;
  input[ReportResultArgIndex(ReportResultArgs::kSellerReportingSignals)] =
      std::make_shared<std::string>(seller_device_signals);
  // This is only added to prevent errors in the ReportResult ad script, and
  // will always be an empty object.
  input[ReportResultArgIndex(ReportResultArgs::kDirectFromSellerSignals)] =
      std::make_shared<std::string>("{}");
  input[ReportResultArgIndex(ReportResultArgs::kEnableAdTechCodeLogging)] =
      std::make_shared<std::string>(
          dispatch_request_config.enable_adtech_code_logging ? "true"
                                                             : "false");
  PS_VLOG(kDispatch, dispatch_request_data.log_context)
      << "\n\nReportResult Input Args:" << "\nAuction Config:\n"
      << *(input[ReportResultArgIndex(ReportResultArgs::kAuctionConfig)])
      << "\nSeller Device Signals:\n"
      << *(input[ReportResultArgIndex(
             ReportResultArgs::kSellerReportingSignals)])
      << "\nEnable AdTech Code Logging:\n"
      << *(input[ReportResultArgIndex(
             ReportResultArgs::kEnableAdTechCodeLogging)])
      << "\nDirect from Seller Signals:\n"
      << *(input[ReportResultArgIndex(
             ReportResultArgs::kDirectFromSellerSignals)]);

  return input;
}

inline DispatchRequest GetReportResultDispatchRequest(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const SellerReportingDispatchRequestData& request_data,
    const std::string& seller_device_signals_json) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  return {.id = request_data.post_auction_signals.winning_ad_render_url,
          .version_string = GetDefaultSellerUdfVersion(),
          .handler_name = kReportResultEntryFunction,
          .input = GetReportResultInput(seller_device_signals_json,
                                        dispatch_request_config, request_data)};
}
}  // namespace
rapidjson::Document GenerateSellerDeviceSignals(
    const SellerReportingDispatchRequestData& dispatch_request_data) {
  rapidjson::Document document(rapidjson::kObjectType);
  // Convert the std::string to a rapidjson::Value object.
  rapidjson::Value hostname_value(
      dispatch_request_data.publisher_hostname.data(), document.GetAllocator());
  rapidjson::Value render_URL_value(
      dispatch_request_data.post_auction_signals.winning_ad_render_url.c_str(),
      document.GetAllocator());
  rapidjson::Value render_url_value(
      dispatch_request_data.post_auction_signals.winning_ad_render_url.c_str(),
      document.GetAllocator());

  rapidjson::Value interest_group_owner(
      dispatch_request_data.post_auction_signals.winning_ig_owner.c_str(),
      document.GetAllocator());

  document.AddMember(kTopWindowHostnameTag, hostname_value.Move(),
                     document.GetAllocator());
  document.AddMember(kInterestGroupOwnerTag, interest_group_owner.Move(),
                     document.GetAllocator());
  document.AddMember(kRenderURLTag, render_URL_value.Move(),
                     document.GetAllocator());
  document.AddMember(kRenderUrlTag, render_url_value.Move(),
                     document.GetAllocator());
  double bid = GetEightBitRoundedValue(
      dispatch_request_data.post_auction_signals.winning_bid);
  if (bid > -1) {
    document.AddMember(kBidTag, bid, document.GetAllocator());
  }
  if (!dispatch_request_data.post_auction_signals.winning_bid_currency
           .empty()) {
    rapidjson::Value winning_bid_currency_value(
        dispatch_request_data.post_auction_signals.winning_bid_currency.c_str(),
        document.GetAllocator());
    document.AddMember(kWinningBidCurrencyTag,
                       winning_bid_currency_value.Move(),
                       document.GetAllocator());
  }
  if (!dispatch_request_data.post_auction_signals
           .highest_scoring_other_bid_currency.empty()) {
    rapidjson::Value highest_scoring_other_bid_currency_value(
        dispatch_request_data.post_auction_signals
            .highest_scoring_other_bid_currency.c_str(),
        document.GetAllocator());
    document.AddMember(kHighestScoringOtherBidCurrencyTag,
                       highest_scoring_other_bid_currency_value.Move(),
                       document.GetAllocator());
  }
  double desirability = GetEightBitRoundedValue(
      dispatch_request_data.post_auction_signals.winning_score);
  if (desirability > -1) {
    document.AddMember(kDesirabilityTag, desirability, document.GetAllocator());
  }
  document.AddMember(
      kHighestScoringOtherBidTag,
      dispatch_request_data.post_auction_signals.highest_scoring_other_bid,
      document.GetAllocator());

  return document;
}

absl::Status PerformReportResult(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    const rapidjson::Document& seller_device_signals,
    const SellerReportingDispatchRequestData& request_data,
    absl::AnyInvocable<
        void(const std::vector<absl::StatusOr<DispatchResponse>>&)>
        report_result_callback,
    CodeDispatchClient& dispatcher) {
  std::string seller_device_signals_json;
  PS_ASSIGN_OR_RETURN(seller_device_signals_json,
                      SerializeJsonDoc(seller_device_signals));
  DispatchRequest dispatch_request = GetReportResultDispatchRequest(
      dispatch_request_config, request_data, seller_device_signals_json);
  dispatch_request.tags[kRomaTimeoutMs] =
      dispatch_request_config.roma_timeout_ms;
  std::vector<DispatchRequest> dispatch_requests = {
      std::move(dispatch_request)};
  return dispatcher.BatchExecute(dispatch_requests,
                                 std::move(report_result_callback));
}

}  // namespace privacy_sandbox::bidding_auction_servers
