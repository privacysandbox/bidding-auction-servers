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

#include "services/auction_service/reporting/seller/top_level_seller_reporting_manager.h"

#include <utility>

#include "absl/status/statusor.h"
#include "rapidjson/document.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/reporting/noiser_and_bucketer.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
// Todo(b/357293697): Refactor SellerReportingDispatchRequestData
rapidjson::Document GenerateSellerDeviceSignalsForTopLevelAuction(
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
  absl::StatusOr<double> rounded_bid = RoundStochasticallyToKBits(
      dispatch_request_data.post_auction_signals.winning_bid,
      kStochasticalRoundingBits);
  if (rounded_bid.ok()) {
    document.AddMember(kBidTag, *rounded_bid, document.GetAllocator());
  } else {
    PS_VLOG(kDispatch) << "Error noising modified bid input to reportResult:"
                       << rounded_bid.status();
  }
  absl::StatusOr<double> desirability = GetEightBitRoundedValue(
      dispatch_request_data.post_auction_signals.winning_score);
  if (desirability.ok()) {
    document.AddMember(kDesirabilityTag, *desirability,
                       document.GetAllocator());
  } else {
    PS_VLOG(kDispatch) << "Error noising desirability input to reportResult:"
                       << desirability.status();
  }
  rapidjson::Value component_seller(
      dispatch_request_data.component_reporting_metadata.component_seller
          .c_str(),
      document.GetAllocator());

  document.AddMember(kComponentSeller, component_seller.Move(),
                     document.GetAllocator());
  return document;
}
}  // namespace privacy_sandbox::bidding_auction_servers
