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
inline constexpr absl::string_view PORT = "BUYER_FRONTEND_PORT";
inline constexpr absl::string_view HEALTHCHECK_PORT =
    "BUYER_FRONTEND_HEALTHCHECK_PORT";
inline constexpr absl::string_view BIDDING_SERVER_ADDR = "BIDDING_SERVER_ADDR";
inline constexpr absl::string_view GRPC_ARG_DEFAULT_AUTHORITY_VAL =
    "GRPC_ARG_DEFAULT_AUTHORITY";
inline constexpr absl::string_view BUYER_KV_SERVER_ADDR =
    "BUYER_KV_SERVER_ADDR";
inline constexpr absl::string_view GENERATE_BID_TIMEOUT_MS =
    "GENERATE_BID_TIMEOUT_MS";
inline constexpr absl::string_view
    PROTECTED_APP_SIGNALS_GENERATE_BID_TIMEOUT_MS =
        "PROTECTED_APP_SIGNALS_GENERATE_BID_TIMEOUT_MS";
inline constexpr absl::string_view BIDDING_SIGNALS_LOAD_TIMEOUT_MS =
    "BIDDING_SIGNALS_LOAD_TIMEOUT_MS";
inline constexpr absl::string_view ENABLE_BUYER_FRONTEND_BENCHMARKING =
    "ENABLE_BUYER_FRONTEND_BENCHMARKING";
inline constexpr absl::string_view CREATE_NEW_EVENT_ENGINE =
    "CREATE_NEW_EVENT_ENGINE";
inline constexpr absl::string_view ENABLE_BIDDING_COMPRESSION =
    "ENABLE_BIDDING_COMPRESSION";
inline constexpr absl::string_view BFE_INGRESS_TLS = "BFE_INGRESS_TLS";
inline constexpr absl::string_view BFE_TLS_KEY = "BFE_TLS_KEY";
inline constexpr absl::string_view BFE_TLS_CERT = "BFE_TLS_CERT";
inline constexpr absl::string_view BIDDING_EGRESS_TLS = "BIDDING_EGRESS_TLS";
inline constexpr absl::string_view
    BFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND =
        "BFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND";
inline constexpr absl::string_view BFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES =
    "BFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES";

inline constexpr int kNumRuntimeFlags = 17;
inline constexpr std::array<absl::string_view, kNumRuntimeFlags> kFlags = {
    PORT,
    HEALTHCHECK_PORT,
    BIDDING_SERVER_ADDR,
    GRPC_ARG_DEFAULT_AUTHORITY_VAL,
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
    BFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND,
    BFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES,
};

inline std::vector<absl::string_view> GetServiceFlags() {
  std::vector<absl::string_view> flags(kFlags.begin(),
                                       kFlags.begin() + kNumRuntimeFlags);

  for (absl::string_view flag : kCommonServiceFlags) {
    flags.push_back(flag);
  }

  return flags;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_RUNTIME_FLAGS_H_
