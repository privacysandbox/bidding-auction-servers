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

#ifndef SERVICES_BUYER_FRONTEND_SERVICE_DATA_GET_BIDS_CONFIG_H_
#define SERVICES_BUYER_FRONTEND_SERVICE_DATA_GET_BIDS_CONFIG_H_

#include <cstdint>

namespace privacy_sandbox::bidding_auction_servers {

enum class BiddingSignalsFetchMode : std::uint8_t {
  REQUIRED = 0,
  FETCHED_BUT_OPTIONAL = 1,
  NOT_FETCHED = 2,
};

struct GetBidsConfig {
  // The max time to wait for generate bid request to finish.
  int generate_bid_timeout_ms;
  // The max time to wait for fetching bidding signals to finish.
  int bidding_signals_load_timeout_ms;
  // The max time to wait for protected app signals generate bid request to
  // finish.
  int protected_app_signals_generate_bid_timeout_ms;
  // Indicates whether Protected App Signals support is enabled or not.
  bool is_protected_app_signals_enabled;
  // Indicates whether Protected Audience support is enabled or not.
  bool is_protected_audience_enabled;
  // Whether chaffing is enabled.
  bool is_chaffing_enabled;
  // Enable v2 for browser
  bool is_tkv_v2_browser_enabled;
  bool enable_cancellation = false;
  bool enable_kanon = false;
  // Sample rate for debug request.
  int debug_sample_rate_micro = 0;
  // make all request consented in non_prod
  bool consent_all_requests = false;
  bool priority_vector_enabled = false;
  bool test_mode = false;
  bool tkv_v2_address_empty = false;
  BiddingSignalsFetchMode bidding_signals_fetch_mode =
      BiddingSignalsFetchMode::REQUIRED;
  // TKV per buyer signals propagation
  bool propagate_buyer_signals_to_tkv = false;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_FRONTEND_SERVICE_DATA_GET_BIDS_CONFIG_H_
