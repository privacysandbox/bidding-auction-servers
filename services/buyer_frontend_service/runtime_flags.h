//  Copyright 2022 Google LLC
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

#ifndef FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_RUNTIME_FLAGS_H_
#define FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_RUNTIME_FLAGS_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "services/common/constants/common_service_flags.h"

namespace privacy_sandbox::bidding_auction_servers {

// Define runtime flag names.
inline constexpr char PORT[] = "BUYER_FRONTEND_PORT";
inline constexpr char HEALTHCHECK_PORT[] = "BUYER_FRONTEND_HEALTHCHECK_PORT";
inline constexpr char BIDDING_SERVER_ADDR[] = "BIDDING_SERVER_ADDR";
inline constexpr char BUYER_KV_SERVER_ADDR[] = "BUYER_KV_SERVER_ADDR";
inline constexpr char GENERATE_BID_TIMEOUT_MS[] = "GENERATE_BID_TIMEOUT_MS";
inline constexpr char PROTECTED_APP_SIGNALS_GENERATE_BID_TIMEOUT_MS[] =
    "PROTECTED_APP_SIGNALS_GENERATE_BID_TIMEOUT_MS";
inline constexpr char BIDDING_SIGNALS_LOAD_TIMEOUT_MS[] =
    "BIDDING_SIGNALS_LOAD_TIMEOUT_MS";
inline constexpr char ENABLE_BUYER_FRONTEND_BENCHMARKING[] =
    "ENABLE_BUYER_FRONTEND_BENCHMARKING";
inline constexpr char CREATE_NEW_EVENT_ENGINE[] = "CREATE_NEW_EVENT_ENGINE";
inline constexpr char ENABLE_BIDDING_COMPRESSION[] =
    "ENABLE_BIDDING_COMPRESSION";
inline constexpr char BFE_INGRESS_TLS[] = "BFE_INGRESS_TLS";
inline constexpr char BFE_TLS_KEY[] = "BFE_TLS_KEY";
inline constexpr char BFE_TLS_CERT[] = "BFE_TLS_CERT";
inline constexpr char BIDDING_EGRESS_TLS[] = "BIDDING_EGRESS_TLS";

inline constexpr absl::string_view kFlags[] = {
    PORT,
    HEALTHCHECK_PORT,
    BIDDING_SERVER_ADDR,
    BUYER_KV_SERVER_ADDR,
    GENERATE_BID_TIMEOUT_MS,
    PROTECTED_APP_SIGNALS_GENERATE_BID_TIMEOUT_MS,
    BIDDING_SIGNALS_LOAD_TIMEOUT_MS,
    ENABLE_BUYER_FRONTEND_BENCHMARKING,
    CREATE_NEW_EVENT_ENGINE,
    ENABLE_BIDDING_COMPRESSION,
    BFE_INGRESS_TLS,
    BFE_TLS_KEY,
    BFE_TLS_CERT,
    BIDDING_EGRESS_TLS,
};

inline std::vector<absl::string_view> GetServiceFlags() {
  int size = sizeof(kFlags) / sizeof(kFlags[0]);
  std::vector<absl::string_view> flags(kFlags, kFlags + size);

  for (absl::string_view flag : kCommonServiceFlags) {
    flags.push_back(flag);
  }

  return flags;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_RUNTIME_FLAGS_H_
