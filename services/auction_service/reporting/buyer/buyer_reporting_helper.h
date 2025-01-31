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

#ifndef SERVICES_AUCTION_SERVICE_REPORTING_BUYER_REPORTING_HELPER_H_
#define SERVICES_AUCTION_SERVICE_REPORTING_BUYER_REPORTING_HELPER_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "rapidjson/document.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/common/private_aggregation/private_aggregation_post_auction_util.h"

namespace privacy_sandbox::bidding_auction_servers {
/* Generates buyerDeviceSignals json input for reportWin() execution.
 * seller_device_signals is expected to be the deserialized rapidjson::Document
 * of sellerDeviceSignals which is input to reportResult() execution.
 * The sellerDeviceSignals is expected to contain a value for "desirability"
 * key. The desirability and modifiedBid are deleted from
 * the seller_device_signals since it is not expected to be provided to the
 * buyer:
 * https://github.com/WICG/turtledove/blob/main/FLEDGE.md#52-buyer-reporting-on-render-and-ad-events
 * When buyer_reporting_id in BuyerReportingDispatchRequestData is set,
 * interest_group_name is optional. All other non-optional fields in
 * BuyerReportingDispatchRequestData are required to be set.
 */
absl::StatusOr<std::shared_ptr<std::string>> GenerateBuyerDeviceSignals(
    const BuyerReportingDispatchRequestData& buyer_reporting_metadata,
    rapidjson::Document& seller_device_signals);

// Parses response from reportWin() udf execution
absl::StatusOr<ReportWinResponse> ParseReportWinResponse(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    absl::string_view response, const BaseValues& base_values,
    RequestLogContext& log_context);

// Sets buyerReportingId, buyerAndSellerReportingId,
// selectedBuyerAndSellerReportingId, or IG name as necessary
void SetReportingIds(
    const BuyerReportingDispatchRequestData& buyer_reporting_metadata,
    rapidjson::Document& seller_device_signals);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_BUYER_REPORTING_HELPER_H_
