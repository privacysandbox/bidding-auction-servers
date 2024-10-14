// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/buyer_frontend_service/util/proto_factory.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
using GetBidsRawRequest = GetBidsRequest::GetBidsRawRequest;
using GetBidsRawResponse = GetBidsResponse::GetBidsRawResponse;
using GenerateBidsRawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using GenerateProtectedAppSignalsBidsRawRequest =
    GenerateProtectedAppSignalsBidsRequest::
        GenerateProtectedAppSignalsBidsRawRequest;
struct ParsedTrustedBiddingSignals {
  std::string json;
  absl::flat_hash_set<std::string> keys;
};

std::unique_ptr<GetBidsRawResponse> CreateGetBidsRawResponse(
    std::unique_ptr<GenerateBidsResponse::GenerateBidsRawResponse>
        raw_response) {
  auto get_bids_raw_response = std::make_unique<GetBidsRawResponse>();

  if (!raw_response->IsInitialized() || raw_response->bids_size() == 0) {
    // Initialize empty list.
    get_bids_raw_response->mutable_bids();
    return get_bids_raw_response;
  }

  get_bids_raw_response->mutable_bids()->Swap(raw_response->mutable_bids());
  return get_bids_raw_response;
}

// Parses trusted bidding signals for a single Interest Group (IG).
// Queries the bidding signals for trusted bidding signal keys in the IG.
// If found,
// 1. Copies key to parsed trusted keys hash set
// 2. Key-value pair to parsed trusted JSON string
absl::StatusOr<ParsedTrustedBiddingSignals> GetSignalsForIG(
    const ::google::protobuf::RepeatedPtrField<std::string>&
        bidding_signals_keys,
    rapidjson::Value& bidding_signals_obj, long avg_signal_str_size) {
  // Insert bidding signal values for this IG.
  ParsedTrustedBiddingSignals parsed_signals;
  rapidjson::Document ig_signals;
  ig_signals.SetObject();

  // Copy bidding signals with key name in bidding signal keys.
  for (const auto& key : bidding_signals_keys) {
    if (parsed_signals.keys.contains(key)) {
      // Do not process duplicate keys.
      continue;
    }
    rapidjson::Value::ConstMemberIterator bidding_signals_key_itr =
        bidding_signals_obj.FindMember(key.c_str());
    if (bidding_signals_key_itr != bidding_signals_obj.MemberEnd()) {
      rapidjson::Value json_key;
      // Keep string reference. Assumes safe lifecycle.
      json_key.SetString(rapidjson::StringRef(key.c_str()));
      rapidjson::Value json_value;
      // Copy instead of move, could be referenced by multiple IGs.
      json_value.CopyFrom(bidding_signals_key_itr->value,
                          ig_signals.GetAllocator());
      // AddMember moves Values, do not reference them anymore.
      ig_signals.AddMember(json_key, json_value, ig_signals.GetAllocator());
      parsed_signals.keys.emplace(key);
    }
  }
  if (ig_signals.MemberCount() > 0) {
    absl::StatusOr<std::string> ig_signals_str =
        SerializeJsonDocToReservedString(ig_signals, avg_signal_str_size);
    PS_ASSIGN_OR_RETURN(parsed_signals.json, ig_signals_str);
  }
  return parsed_signals;
}

// Copy properties from IG from device to IG for Bidding.
// Note: trusted bidding signals and keys are not copied.
void CopyIGFromDeviceToIGForBidding(
    const BuyerInput::InterestGroup& ig_from_device,
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding*
        mutable_ig_for_bidding) {
  mutable_ig_for_bidding->set_name(ig_from_device.name());

  if (!ig_from_device.user_bidding_signals().empty()) {
    mutable_ig_for_bidding->set_user_bidding_signals(
        ig_from_device.user_bidding_signals());
  }

  if (!ig_from_device.ad_render_ids().empty()) {
    mutable_ig_for_bidding->mutable_ad_render_ids()->CopyFrom(
        ig_from_device.ad_render_ids());
  }

  if (!ig_from_device.component_ads().empty()) {
    mutable_ig_for_bidding->mutable_ad_component_render_ids()->CopyFrom(
        ig_from_device.component_ads());
  }

  // Set device signals.
  if (ig_from_device.has_browser_signals() &&
      ig_from_device.browser_signals().IsInitialized()) {
    mutable_ig_for_bidding->mutable_browser_signals()->CopyFrom(
        ig_from_device.browser_signals());
  } else if (ig_from_device.has_android_signals()) {
    mutable_ig_for_bidding->mutable_android_signals()->CopyFrom(
        ig_from_device.android_signals());
  }
}

std::unique_ptr<GenerateBidsRawRequest> CreateGenerateBidsRawRequest(
    const GetBidsRequest::GetBidsRawRequest& get_bids_raw_request,
    std::unique_ptr<rapidjson::Value> bidding_signals_obj,
    const size_t signal_size, const bool enable_kanon) {
  auto generate_bids_raw_request = std::make_unique<GenerateBidsRawRequest>();
  const BuyerInput& buyer_input = get_bids_raw_request.buyer_input();

  // 1. Set interest groups (IGs) for bidding.
  if (buyer_input.interest_groups_size() > 0) {
    long avg_signal_size_per_ig =
        signal_size / buyer_input.interest_groups_size();
    // Iterate through each IG from device.
    for (int i = 0; i < buyer_input.interest_groups_size(); i++) {
      // Skip IG if it has no name or bidding signals keys.
      const auto& ig_from_device = buyer_input.interest_groups(i);
      if (ig_from_device.name().empty() ||
          ig_from_device.bidding_signals_keys().empty()) {
        continue;
      }
      // Get parsed trusted bidding signals for this IG.
      absl::StatusOr<ParsedTrustedBiddingSignals> parsed_signals =
          GetSignalsForIG(ig_from_device.bidding_signals_keys(),
                          *bidding_signals_obj, avg_signal_size_per_ig);
      // Skip IG if it has no parsed trusted bidding signals.
      if (!parsed_signals.ok() || parsed_signals->json.empty()) {
        continue;
      }
      // Initialize IG for bidding.
      auto mutable_ig_for_bidding =
          generate_bids_raw_request->mutable_interest_group_for_bidding()
              ->Add();
      // Only add trusted bidding signals keys that are parsed.
      // TODO(b/308793587): Optimize.
      for (const std::string& key : parsed_signals->keys) {
        mutable_ig_for_bidding->add_trusted_bidding_signals_keys(key);
      }
      // Set trusted bidding signals to include only those signals that are
      // parsed.
      mutable_ig_for_bidding->set_trusted_bidding_signals(
          std::move(parsed_signals->json));
      // Copy other properties from IG from device to IG for bidding.
      CopyIGFromDeviceToIGForBidding(ig_from_device, mutable_ig_for_bidding);
    }
  }

  // 2. Set auction signals.
  generate_bids_raw_request->set_auction_signals(
      get_bids_raw_request.auction_signals());

  // 3. Set buyer signals.
  if (!get_bids_raw_request.buyer_signals().empty()) {
    generate_bids_raw_request->set_buyer_signals(
        get_bids_raw_request.buyer_signals());
  } else {
    generate_bids_raw_request->set_buyer_signals("");
  }

  // 5. Set debug reporting flag.
  generate_bids_raw_request->set_enable_debug_reporting(
      get_bids_raw_request.enable_debug_reporting());

  // 6. Set seller domain.
  generate_bids_raw_request->set_seller(get_bids_raw_request.seller());

  // 7. Set publisher name.
  generate_bids_raw_request->set_publisher_name(
      get_bids_raw_request.publisher_name());

  // 8. Set logging context.
  if (!get_bids_raw_request.log_context().adtech_debug_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_adtech_debug_id(
        get_bids_raw_request.log_context().adtech_debug_id());
  }
  if (!get_bids_raw_request.log_context().generation_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_generation_id(
        get_bids_raw_request.log_context().generation_id());
  }

  // 9. Set consented debug config.
  if (get_bids_raw_request.has_consented_debug_config()) {
    *generate_bids_raw_request->mutable_consented_debug_config() =
        get_bids_raw_request.consented_debug_config();
  }

  // 10. Set top level seller.
  generate_bids_raw_request->set_top_level_seller(
      get_bids_raw_request.top_level_seller());

  // 11. Set multi bid limit.
  generate_bids_raw_request->set_multi_bid_limit(kDefaultMultiBidLimit);
  if (enable_kanon) {
    generate_bids_raw_request->set_enforce_kanon(
        get_bids_raw_request.enforce_kanon());
    int bid_limit = get_bids_raw_request.multi_bid_limit();
    if (bid_limit > 0) {
      generate_bids_raw_request->set_multi_bid_limit(bid_limit);
    }
  }
  return generate_bids_raw_request;
}

std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>
CreateGenerateProtectedAppSignalsBidsRawRequest(
    const GetBidsRawRequest& raw_request, const bool enable_kanon) {
  auto generate_bids_raw_request =
      std::make_unique<GenerateProtectedAppSignalsBidsRawRequest>();
  generate_bids_raw_request->set_auction_signals(raw_request.auction_signals());

  if (!raw_request.buyer_signals().empty()) {
    generate_bids_raw_request->set_buyer_signals(raw_request.buyer_signals());
  } else {
    generate_bids_raw_request->set_buyer_signals("");
  }

  *generate_bids_raw_request->mutable_protected_app_signals() =
      raw_request.protected_app_signals_buyer_input().protected_app_signals();
  if (raw_request.protected_app_signals_buyer_input()
          .has_contextual_protected_app_signals_data()) {
    *generate_bids_raw_request
         ->mutable_contextual_protected_app_signals_data() =
        raw_request.protected_app_signals_buyer_input()
            .contextual_protected_app_signals_data();
  }

  generate_bids_raw_request->set_seller(raw_request.seller());

  generate_bids_raw_request->set_publisher_name(raw_request.publisher_name());

  if (!raw_request.log_context().adtech_debug_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_adtech_debug_id(
        raw_request.log_context().adtech_debug_id());
  }
  if (!raw_request.log_context().generation_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_generation_id(
        raw_request.log_context().generation_id());
  }

  if (raw_request.has_consented_debug_config()) {
    *generate_bids_raw_request->mutable_consented_debug_config() =
        raw_request.consented_debug_config();
  }

  generate_bids_raw_request->set_enable_debug_reporting(
      raw_request.enable_debug_reporting());

  generate_bids_raw_request->set_enable_unlimited_egress(
      raw_request.enable_unlimited_egress());

  generate_bids_raw_request->set_multi_bid_limit(kDefaultMultiBidLimit);
  if (enable_kanon) {
    generate_bids_raw_request->set_enforce_kanon(raw_request.enforce_kanon());
    int bid_limit = raw_request.multi_bid_limit();
    if (bid_limit > 0) {
      generate_bids_raw_request->set_multi_bid_limit(bid_limit);
    }
  }

  return generate_bids_raw_request;
}

}  // namespace privacy_sandbox::bidding_auction_servers
