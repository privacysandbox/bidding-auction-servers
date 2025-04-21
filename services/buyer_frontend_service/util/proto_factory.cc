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

#include <algorithm>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "services/common/util/json_util.h"
#include "services/common/util/priority_vector/priority_vector_utils.h"
#include "services/common/util/request_response_constants.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using GetBidsRawRequest = GetBidsRequest::GetBidsRawRequest;
using GetBidsRawResponse = GetBidsResponse::GetBidsRawResponse;
using GenerateBidsRawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using GenerateProtectedAppSignalsBidsRawRequest =
    GenerateProtectedAppSignalsBidsRequest::
        GenerateProtectedAppSignalsBidsRawRequest;
using InterestGroupForBidding = GenerateBidsRawRequest::InterestGroupForBidding;
struct ParsedTrustedBiddingSignals {
  std::string json;
  absl::flat_hash_set<std::string> keys;
};

inline std::string SerializeSignalsMap(
    const absl::flat_hash_map<absl::string_view, absl::string_view>& signals) {
  return absl::StrCat(
      "{",
      absl::StrJoin(
          signals, ",",
          [](std::string* out,
             std::pair<absl::string_view, absl::string_view> key_val) {
            out->append(
                absl::StrCat("\"", key_val.first, "\"", ":", key_val.second));
          }),
      "}");
}

}  // namespace

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
    const absl::flat_hash_map<std::string, std::string>& bidding_signals_obj) {
  // Insert bidding signal values for this IG.
  ParsedTrustedBiddingSignals parsed_signals;
  absl::flat_hash_map<absl::string_view, absl::string_view> ig_signals;

  // Copy bidding signals with key name in bidding signal keys.
  for (const auto& key : bidding_signals_keys) {
    if (parsed_signals.keys.contains(key)) {
      // Do not process duplicate keys.
      continue;
    }
    auto bidding_signals_key_itr = bidding_signals_obj.find(key);
    if (bidding_signals_key_itr != bidding_signals_obj.end()) {
      ig_signals[key] = bidding_signals_key_itr->second;
      parsed_signals.keys.emplace(key);
    }
  }
  if (!ig_signals.empty()) {
    parsed_signals.json = SerializeSignalsMap(ig_signals);
  }
  return parsed_signals;
}

// Copy properties from IG from device to IG for Bidding.
// Note: trusted bidding signals and keys are not copied.
void CopyIGFromDeviceToIGForBidding(
    BuyerInputForBidding::InterestGroupForBidding&& ig_from_device,
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding*
        mutable_ig_for_bidding) {
  *mutable_ig_for_bidding->mutable_name() =
      std::move(*ig_from_device.mutable_name());

  if (!ig_from_device.user_bidding_signals().empty()) {
    *mutable_ig_for_bidding->mutable_user_bidding_signals() =
        std::move(*ig_from_device.mutable_user_bidding_signals());
  }

  if (!ig_from_device.ad_render_ids().empty()) {
    mutable_ig_for_bidding->mutable_ad_render_ids()->Swap(
        ig_from_device.mutable_ad_render_ids());
  }

  if (!ig_from_device.component_ads().empty()) {
    mutable_ig_for_bidding->mutable_ad_component_render_ids()->Swap(
        ig_from_device.mutable_component_ads());
  }

  // Set device signals.
  if (ig_from_device.has_browser_signals() &&
      ig_from_device.browser_signals().IsInitialized()) {
    mutable_ig_for_bidding->mutable_browser_signals_for_bidding()->Swap(
        ig_from_device.mutable_browser_signals());
  } else if (ig_from_device.has_android_signals()) {
    mutable_ig_for_bidding->mutable_android_signals_for_bidding()->Swap(
        ig_from_device.mutable_android_signals());
  }
}

PrepareGenerateBidsRequestResult PrepareGenerateBidsRequest(
    GetBidsRequest::GetBidsRawRequest& get_bids_raw_request,
    const absl::flat_hash_map<std::string, std::string>& bidding_signals_obj,
    const size_t signal_size, uint32_t data_version,
    const PriorityVectorConfig& priority_vector_config,
    server_common::Executor& executor,
    const PrepareGenerateBidsRequestOptions& options) {
  auto generate_bids_raw_request = std::make_unique<GenerateBidsRawRequest>();
  BuyerInputForBidding& buyer_input =
      *get_bids_raw_request.mutable_buyer_input_for_bidding();
  absl::flat_hash_map<std::string, double> interest_group_priorities;
  if (priority_vector_config.priority_vector_enabled) {
    interest_group_priorities = CalculateInterestGroupPriorities(
        priority_vector_config.priority_signals, buyer_input,
        priority_vector_config.per_ig_priority_vectors);
  }

  // 1. Set interest groups (IGs) for bidding.
  std::atomic<int> num_filtered_igs = 0;
  absl::Mutex generate_bids_raw_request_mu;
  if (!buyer_input.interest_groups().empty()) {
    absl::BlockingCounter done(buyer_input.interest_groups().size());
    for (auto&& ig_from_device : *buyer_input.mutable_interest_groups()) {
      executor.Run([&generate_bids_raw_request_mu, &generate_bids_raw_request,
                    &done, &bidding_signals_obj, &options,
                    &priority_vector_config, &interest_group_priorities,
                    &num_filtered_igs,
                    ig_from_device = std::move(ig_from_device)]() mutable {
        // Skip IG if it has no name or if bidding signals are required but the
        // IG has no bidding signals keys.
        if (ig_from_device.name().empty() ||
            (options.require_bidding_signals &&
             ig_from_device.bidding_signals_keys().empty())) {
          done.DecrementCount();
          return;
        }

        // Filter IGs with negative priority when PV is enabled.
        if (priority_vector_config.priority_vector_enabled &&
            interest_group_priorities[ig_from_device.name()] < 0) {
          PS_VLOG(8) << absl::StrFormat(
              "Filtered out IG from bid generation: (IG name: %s, priority: "
              "%f)",
              ig_from_device.name(),
              interest_group_priorities[ig_from_device.name()]);
          num_filtered_igs++;
          done.DecrementCount();
          return;
        }

        // Get parsed trusted bidding signals for this IG.
        absl::StatusOr<ParsedTrustedBiddingSignals> parsed_signals;
        if (!bidding_signals_obj.empty()) {
          parsed_signals = GetSignalsForIG(
              ig_from_device.bidding_signals_keys(), bidding_signals_obj);
        }

        // Skip IG if bidding signals are required but the IG has no parsed
        // trusted bidding signals.
        bool has_parsed_signals =
            parsed_signals.ok() && !parsed_signals->json.empty();
        if (options.require_bidding_signals && !has_parsed_signals) {
          done.DecrementCount();
          return;
        }

        // Initialize IG for bidding.
        InterestGroupForBidding* mutable_ig_for_bidding = nullptr;
        {
          absl::MutexLock lock(&generate_bids_raw_request_mu);
          mutable_ig_for_bidding =
              generate_bids_raw_request->mutable_interest_group_for_bidding()
                  ->Add();
        }
        if (has_parsed_signals) {
          // Only add trusted bidding signals keys that are parsed.
          // TODO(b/308793587): Optimize.
          for (const std::string& key : parsed_signals->keys) {
            mutable_ig_for_bidding->add_trusted_bidding_signals_keys(key);
          }
          // Set trusted bidding signals to include only those signals that are
          // parsed.
          mutable_ig_for_bidding->set_trusted_bidding_signals(
              std::move(parsed_signals->json));
        } else {
          mutable_ig_for_bidding->set_trusted_bidding_signals(
              kNullBiddingSignalsJson);
        }

        // Copy other properties from IG from device to IG for bidding.
        CopyIGFromDeviceToIGForBidding(std::move(ig_from_device),
                                       mutable_ig_for_bidding);
        done.DecrementCount();
      });
    }
    done.Wait();
  }

  // Set auction signals.
  generate_bids_raw_request->set_auction_signals(
      get_bids_raw_request.auction_signals());

  // Set buyer signals.
  if (!get_bids_raw_request.buyer_signals().empty()) {
    generate_bids_raw_request->set_buyer_signals(
        get_bids_raw_request.buyer_signals());
  } else {
    generate_bids_raw_request->set_buyer_signals("");
  }

  // Set debug reporting flags.
  generate_bids_raw_request->set_enable_debug_reporting(
      get_bids_raw_request.enable_debug_reporting());
  if (get_bids_raw_request.enable_sampled_debug_reporting()) {
    generate_bids_raw_request->mutable_fdo_flags()
        ->set_enable_sampled_debug_reporting(true);
  }
  if (buyer_input.in_cooldown_or_lockout()) {
    generate_bids_raw_request->mutable_fdo_flags()->set_in_cooldown_or_lockout(
        true);
  }

  // Set seller and top level seller domains.
  generate_bids_raw_request->set_seller(get_bids_raw_request.seller());
  generate_bids_raw_request->set_top_level_seller(
      get_bids_raw_request.top_level_seller());

  // Set publisher name.
  generate_bids_raw_request->set_publisher_name(
      get_bids_raw_request.publisher_name());

  // Set logging context.
  if (!get_bids_raw_request.log_context().adtech_debug_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_adtech_debug_id(
        get_bids_raw_request.log_context().adtech_debug_id());
  }
  if (!get_bids_raw_request.log_context().generation_id().empty()) {
    generate_bids_raw_request->mutable_log_context()->set_generation_id(
        get_bids_raw_request.log_context().generation_id());
  }

  // Set consented debug config.
  if (get_bids_raw_request.has_consented_debug_config()) {
    *generate_bids_raw_request->mutable_consented_debug_config() =
        get_bids_raw_request.consented_debug_config();
  }

  // Set kanon flags.
  generate_bids_raw_request->set_multi_bid_limit(kDefaultMultiBidLimit);
  if (options.enable_kanon) {
    generate_bids_raw_request->set_enforce_kanon(
        get_bids_raw_request.enforce_kanon());
    int bid_limit = get_bids_raw_request.multi_bid_limit();
    if (bid_limit > 0) {
      generate_bids_raw_request->set_multi_bid_limit(bid_limit);
    }
  }

  // Set data version.
  generate_bids_raw_request->set_data_version(data_version);

  // Set blob versions.
  if (get_bids_raw_request.has_blob_versions()) {
    *generate_bids_raw_request->mutable_blob_versions() =
        get_bids_raw_request.blob_versions();
  }

  double percent_igs_filtered = static_cast<double>(num_filtered_igs.load()) /
                                std::max(1, buyer_input.interest_groups_size());
  return {.raw_request = std::move(generate_bids_raw_request),
          .percent_igs_filtered = percent_igs_filtered};
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

  generate_bids_raw_request->set_top_level_seller(
      raw_request.top_level_seller());

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

  if (raw_request.has_blob_versions()) {
    *generate_bids_raw_request->mutable_blob_versions() =
        raw_request.blob_versions();
  }

  return generate_bids_raw_request;
}

}  // namespace privacy_sandbox::bidding_auction_servers
