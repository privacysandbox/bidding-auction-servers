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

namespace privacy_sandbox::bidding_auction_servers {

struct BiddingServiceRuntimeConfig {
  // Enables request decryption and response encryption.
  bool encryption_enabled = false;

  bool enable_buyer_debug_url_generation = false;
  // Sets the timeout used by Roma for dispatch requests
  std::string roma_timeout_ms = "10000";
  // Enables Buyer Code Wrapper for wrapping the AdTech code before loading it
  // in Roma. This wrapper can be used to enable multiple features such as :
  // - Exporting console.logs from Roma
  // - Event level debug win and loss reporting
  bool enable_buyer_code_wrapper = false;
  // Enables exporting console.logs from Roma to Bidding Service
  bool enable_adtech_code_logging = false;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_DATA_RUNTIME_CONFIG_H_
