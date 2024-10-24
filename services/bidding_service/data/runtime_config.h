/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_BIDDING_SERVICE_DATA_RUNTIME_CONFIG_H_
#define SERVICES_BIDDING_SERVICE_DATA_RUNTIME_CONFIG_H_

#include <string>

#include "services/bidding_service/constants.h"

namespace privacy_sandbox::bidding_auction_servers {

struct BiddingServiceRuntimeConfig {
  // Endpoint where the ad retrieval server is listening for protected app
  // signals. Must be present iff running with protected app signals support.
  std::string tee_ad_retrieval_kv_server_addr = "";
  // Authority header value for said endpoint.
  std::string tee_ad_retrieval_kv_server_grpc_arg_default_authority = "";

  // Endpoint where ads metadata can be retrieved using PAS ad ids.
  std::string tee_kv_server_addr = "";
  // Authority header value for said endpoint.
  std::string tee_kv_server_grpc_arg_default_authority = "";

  bool enable_buyer_debug_url_generation = false;
  // Sets the timeout used by Roma for dispatch requests
  std::string roma_timeout_ms = "10000";
  // Enables Buyer Code Wrapper for wrapping the AdTech code before loading it
  // in Roma. This wrapper can be used to enable multiple features such as :
  // - Exporting console.logs from Roma
  // - Event level debug win and loss reporting
  bool enable_buyer_code_wrapper = false;
  // Indicates whether or not protected app signals support is enabled.
  bool is_protected_app_signals_enabled = false;
  // Indicates whether or not Protected Audience support is enabled.
  bool is_protected_audience_enabled = true;
  // Time to wait for the ad retrieval request to complete.
  int ad_retrieval_timeout_ms = 60000;
  // The max allowed size of a debug win or loss URL. Default value is 64 KB.
  int max_allowed_size_debug_url_bytes = 65536;
  // The max allowed size of all debug win or loss URLs for an auction.
  // Default value is 3000 kilobytes.
  int max_allowed_size_all_debug_urls_kb = 3000;
  // Whether GRPC client to ad retrieval server should use TLS.
  bool ad_retrieval_kv_server_egress_tls = true;
  // Whether GRPC client to KV server should use TLS.
  bool kv_server_egress_tls = true;
  // Default UDF versions:
  std::string default_protected_auction_generate_bid_version =
      kProtectedAudienceGenerateBidBlobVersion;
  std::string default_protected_app_signals_generate_bid_version =
      kProtectedAppSignalsGenerateBidBlobVersion;
  std::string default_ad_retrieval_version =
      kPrepareDataForAdRetrievalBlobVersion;
  // Enables private aggregate reporting.
  bool enable_private_aggregate_reporting = false;

  bool enable_cancellation = false;
  bool enable_kanon = false;
  bool enable_temporary_unlimited_egress = false;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_DATA_RUNTIME_CONFIG_H_
