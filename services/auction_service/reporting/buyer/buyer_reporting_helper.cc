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

#include "services/auction_service/reporting/buyer/buyer_reporting_helper.h"

#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "services/auction_service/reporting/noiser_and_bucketer.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
constexpr absl::string_view kReportWinUDFName = "reportWin";
}  // namespace

void SetBuyerReportingIds(
    const BuyerReportingDispatchRequestData& buyer_reporting_metadata,
    rapidjson::Document& seller_device_signals) {
  // if buyerAndSellerReportingId is present,
  // it will be used as a substitute for the IG Name. else if buyerReportingId
  // is present, it will be used as a substitute for the IG Name. else, the
  // implementation will fallback to using IG name. Reference:
  // https://github.com/WICG/turtledove/blob/main/FLEDGE.md#12-interest-group-attributes
  if (buyer_reporting_metadata.buyer_and_seller_reporting_id) {
    rapidjson::GenericStringRef buyer_and_seller_reporting_id(
        (buyer_reporting_metadata.buyer_and_seller_reporting_id)->c_str());
    seller_device_signals.AddMember(kBuyerAndSellerReportingIdTag,
                                    buyer_and_seller_reporting_id,
                                    seller_device_signals.GetAllocator());
  } else if (buyer_reporting_metadata.buyer_reporting_id) {
    rapidjson::GenericStringRef buyer_reporting_id(
        (buyer_reporting_metadata.buyer_reporting_id)->c_str());
    seller_device_signals.AddMember(kBuyerReportingIdTag, buyer_reporting_id,
                                    seller_device_signals.GetAllocator());
  } else {
    rapidjson::GenericStringRef interest_group_name(
        buyer_reporting_metadata.interest_group_name.c_str());
    seller_device_signals.AddMember(kInterestGroupNameTag, interest_group_name,
                                    seller_device_signals.GetAllocator());
  }
}

absl::StatusOr<std::shared_ptr<std::string>> GenerateBuyerDeviceSignals(
    const BuyerReportingDispatchRequestData& buyer_reporting_metadata,
    rapidjson::Document& seller_device_signals) {
  // Buyer's device signals are not expected to contain desirability and
  // modifiedBid:
  // https://github.com/privacysandbox/protected-auction-services-docs/blob/main/bidding_auction_multiseller_event_level_reporting.md
  seller_device_signals.RemoveMember(kDesirabilityTag);
  seller_device_signals.RemoveMember(kModifiedBid);
  seller_device_signals.AddMember(
      kMadeHighestScoringOtherBid,
      buyer_reporting_metadata.made_highest_scoring_other_bid,
      seller_device_signals.GetAllocator());
  if (buyer_reporting_metadata.join_count) {
    int join_count = *buyer_reporting_metadata.join_count;
    absl::StatusOr<int> noised_join_count = NoiseAndBucketJoinCount(join_count);
    if (noised_join_count.ok()) {
      join_count = *noised_join_count;
      seller_device_signals.AddMember(kJoinCountTag, join_count,
                                      seller_device_signals.GetAllocator());
    } else {
      // TODO(b/311472988): Add metrics for noising errors.
      PS_VLOG(kDispatch, buyer_reporting_metadata.log_context)
          << "Error with noising join_count in buyerDeviceSignals for "
             "reporting";
    }
  }
  if (buyer_reporting_metadata.recency) {
    long recency = *buyer_reporting_metadata.recency;
    absl::StatusOr<long> noised_recency = NoiseAndBucketRecency(recency);
    if (noised_recency.ok()) {
      recency = *noised_recency;
      seller_device_signals.AddMember(kRecencyTag, recency,
                                      seller_device_signals.GetAllocator());
    } else {
      // TODO(b/311472988): Add metrics for noising errors.
      PS_VLOG(kDispatch, buyer_reporting_metadata.log_context)
          << "Error with noising recency in buyerDeviceSignals for reporting";
    }
  }
  if (buyer_reporting_metadata.modeling_signals) {
    int modeling_signals = *buyer_reporting_metadata.modeling_signals;
    absl::StatusOr<int> noised_modeling_signals =
        NoiseAndMaskModelingSignals(modeling_signals);
    if (noised_modeling_signals.ok()) {
      modeling_signals = *noised_modeling_signals;
      seller_device_signals.AddMember(kModelingSignalsTag, modeling_signals,
                                      seller_device_signals.GetAllocator());
    } else {
      // TODO(b/311472988): Add metrics for noising errors.
      PS_VLOG(kDispatch, buyer_reporting_metadata.log_context)
          << "Error with noising modeling signals in buyerDeviceSignals for "
             "reporting";
    }
  }
  if (!buyer_reporting_metadata.seller.empty()) {
    rapidjson::GenericStringRef seller(buyer_reporting_metadata.seller.c_str());
    seller_device_signals.AddMember(kSellerTag, seller,
                                    seller_device_signals.GetAllocator());
  }
  absl::StatusOr<double> ad_cost = RoundStochasticallyToKBits(
      buyer_reporting_metadata.ad_cost, kStochasticalRoundingBits);
  if (ad_cost.ok()) {
    seller_device_signals.AddMember(kAdCostTag, *ad_cost,
                                    seller_device_signals.GetAllocator());
  }
  SetBuyerReportingIds(buyer_reporting_metadata, seller_device_signals);

  return SerializeJsonDoc(seller_device_signals,
                          buyer_reporting_metadata.buyer_signals.size());
}

absl::StatusOr<ReportWinResponse> ParseReportWinResponse(
    const ReportingDispatchRequestConfig& dispatch_request_config,
    absl::string_view response, RequestLogContext& log_context) {
  PS_ASSIGN_OR_RETURN(rapidjson::Document document, ParseJsonString(response));
  auto it = document.FindMember(kResponse);
  if (it == document.MemberEnd()) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Unexpected response from reportWin execution. "
                        "\"response\" field not found.");
  }
  const auto& response_obj = it->value.GetObject();
  ReportWinResponse report_win_response;
  PS_ASSIGN_IF_PRESENT(report_win_response.report_win_url, response_obj,
                       kReportWinUrl, String);
  rapidjson::Value interaction_reporting_urls_map;
  PS_ASSIGN_IF_PRESENT(interaction_reporting_urls_map, response_obj,
                       kInteractionReportingUrlsWrapperResponse, Object);
  for (rapidjson::Value::MemberIterator it =
           interaction_reporting_urls_map.MemberBegin();
       it != interaction_reporting_urls_map.MemberEnd(); ++it) {
    const auto& [key, value] = *it;
    auto [iterator, inserted] =
        report_win_response.interaction_reporting_urls.try_emplace(
            key.GetString(), value.GetString());
    if (!inserted) {
      PS_VLOG(kDispatch, log_context)
          << "Error inserting interaction reporting url for reportWin()."
          << iterator->first
          << " event already found with url:" << iterator->second;
    }
  }
  if (dispatch_request_config.enable_adtech_code_logging) {
    HandleUdfLogs(document, kReportingUdfLogs, kReportWinUDFName, log_context);
    HandleUdfLogs(document, kReportingUdfErrors, kReportWinUDFName,
                  log_context);
    HandleUdfLogs(document, kReportingUdfWarnings, kReportWinUDFName,
                  log_context);
  }
  return report_win_response;
}

}  // namespace privacy_sandbox::bidding_auction_servers
