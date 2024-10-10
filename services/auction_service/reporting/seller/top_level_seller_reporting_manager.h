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

#ifndef SERVICES_AUCTION_SERVICE_TOP_LEVEL_SELLER_REPORTING_MANAGER_H_
#define SERVICES_AUCTION_SERVICE_TOP_LEVEL_SELLER_REPORTING_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "rapidjson/document.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"

namespace privacy_sandbox::bidding_auction_servers {

// Generates device signals for reportResult input for top level seller
// Expects post_auction_signals to have all required fields.
// Does not validate fields in post_auction_signals in request_data.
rapidjson::Document GenerateSellerDeviceSignalsForTopLevelAuction(
    const SellerReportingDispatchRequestData& request_data);
}  // namespace privacy_sandbox::bidding_auction_servers
#endif
// SERVICES_AUCTION_SERVICE_TOP_LEVEL_SELLER_REPORTING_MANAGER_H_
