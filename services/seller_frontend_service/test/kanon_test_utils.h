/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_TEST_KANON_TEST_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_TEST_KANON_TEST_UTILS_H_

#include <memory>
#include <string>
#include <vector>

#include "services/seller_frontend_service/data/k_anon.h"

namespace privacy_sandbox::bidding_auction_servers {

struct KAnonAuctionResultDataInputs {
  int ig_index = -1;
  std::string ig_owner;
  std::string ig_name;
  std::vector<uint8_t> bucket_name;
  int bucket_value = -1;
  std::string ad_render_url;
  std::string ad_component_render_url;
  float modified_bid = 1.00;
  std::string bid_currency;
  std::string ad_metadata;
  std::string buyer_reporting_id;
  std::string buyer_and_seller_reporting_id;
  std::string selected_buyer_and_seller_reporting_id;
  std::vector<uint8_t> ad_render_url_hash;
  std::vector<uint8_t> ad_component_render_urls_hash;
  std::vector<uint8_t> reporting_id_hash;
  int winner_positional_index = -1;
};

std::unique_ptr<KAnonAuctionResultData> SampleKAnonAuctionResultData(
    KAnonAuctionResultDataInputs inputs);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_TEST_KANON_TEST_UTILS_H_
