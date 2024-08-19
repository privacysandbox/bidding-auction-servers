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

#include "services/auction_service/reporting/seller/component_seller_reporting_manager.h"

#include <utility>

#include "absl/status/statusor.h"
#include "rapidjson/document.h"
#include "services/auction_service/reporting/noiser_and_bucketer.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/reporting/seller/seller_reporting_manager.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
rapidjson::Document GenerateSellerDeviceSignalsForComponentAuction(
    const SellerReportingDispatchRequestData& dispatch_request_data) {
  rapidjson::Document document =
      GenerateSellerDeviceSignals(dispatch_request_data);
  rapidjson::Value top_level_seller(
      dispatch_request_data.component_reporting_metadata.top_level_seller
          .c_str(),
      document.GetAllocator());

  document.AddMember(kTopLevelSellerTag, top_level_seller.Move(),
                     document.GetAllocator());
  absl::StatusOr<double> modified_bid = RoundStochasticallyToKBits(
      dispatch_request_data.component_reporting_metadata.modified_bid,
      kStochasticalRoundingBits);
  if (modified_bid.ok()) {
    document.AddMember(kModifiedBid, *modified_bid, document.GetAllocator());
  } else {
    PS_VLOG(kDispatch) << "Error noising modified bid input to reportResult:"
                       << modified_bid.status();
  }
  return document;
}
}  // namespace privacy_sandbox::bidding_auction_servers
