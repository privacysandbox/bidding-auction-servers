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

#ifndef FLEDGE_SERVICES_SELLER_FRONTEND_SERVICE_RUNTIME_FLAGS_H_
#define FLEDGE_SERVICES_SELLER_FRONTEND_SERVICE_RUNTIME_FLAGS_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "services/common/constants/common_service_flags.h"

namespace privacy_sandbox::bidding_auction_servers {

// Define runtime flag names.
inline constexpr absl::string_view PORT = "SELLER_FRONTEND_PORT";
inline constexpr absl::string_view HEALTHCHECK_PORT =
    "SELLER_FRONTEND_HEALTHCHECK_PORT";
inline constexpr absl::string_view GET_BID_RPC_TIMEOUT_MS =
    "GET_BID_RPC_TIMEOUT_MS";
inline constexpr absl::string_view KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS =
    "KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS";
inline constexpr absl::string_view SCORE_ADS_RPC_TIMEOUT_MS =
    "SCORE_ADS_RPC_TIMEOUT_MS";
inline constexpr absl::string_view SELLER_ORIGIN_DOMAIN =
    "SELLER_ORIGIN_DOMAIN";
inline constexpr absl::string_view AUCTION_SERVER_HOST = "AUCTION_SERVER_HOST";
inline constexpr absl::string_view GRPC_ARG_DEFAULT_AUTHORITY_VAL =
    "GRPC_ARG_DEFAULT_AUTHORITY";
inline constexpr absl::string_view KEY_VALUE_SIGNALS_HOST =
    "KEY_VALUE_SIGNALS_HOST";
inline constexpr absl::string_view TRUSTED_KEY_VALUE_V2_SIGNALS_HOST =
    "TRUSTED_KEY_VALUE_V2_SIGNALS_HOST";
inline constexpr absl::string_view BUYER_SERVER_HOSTS = "BUYER_SERVER_HOSTS";
inline constexpr absl::string_view ENABLE_BUYER_COMPRESSION =
    "ENABLE_BUYER_COMPRESSION";
inline constexpr absl::string_view ENABLE_AUCTION_COMPRESSION =
    "ENABLE_AUCTION_COMPRESSION";
inline constexpr absl::string_view ENABLE_SELLER_FRONTEND_BENCHMARKING =
    "ENABLE_SELLER_FRONTEND_BENCHMARKING";
inline constexpr absl::string_view CREATE_NEW_EVENT_ENGINE =
    "CREATE_NEW_EVENT_ENGINE";
inline constexpr absl::string_view SFE_INGRESS_TLS = "SFE_INGRESS_TLS";
inline constexpr absl::string_view SFE_TLS_KEY = "SFE_TLS_KEY";
inline constexpr absl::string_view SFE_TLS_CERT = "SFE_TLS_CERT";
inline constexpr absl::string_view AUCTION_EGRESS_TLS = "AUCTION_EGRESS_TLS";
inline constexpr absl::string_view BUYER_EGRESS_TLS = "BUYER_EGRESS_TLS";
inline constexpr absl::string_view SFE_PUBLIC_KEYS_ENDPOINTS =
    "SFE_PUBLIC_KEYS_ENDPOINTS";
inline constexpr absl::string_view SELLER_CLOUD_PLATFORMS_MAP =
    "SELLER_CLOUD_PLATFORMS_MAP";
inline constexpr absl::string_view
    SFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND =
        "SFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND";
inline constexpr absl::string_view SFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES =
    "SFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES";
inline constexpr absl::string_view K_ANON_API_KEY = "K_ANON_API_KEY";
inline constexpr absl::string_view ALLOW_COMPRESSED_AUCTION_CONFIG =
    "ALLOW_COMPRESSED_AUCTION_CONFIG";
inline constexpr absl::string_view SCORING_SIGNALS_FETCH_MODE =
    "SCORING_SIGNALS_FETCH_MODE";
inline constexpr absl::string_view HEADER_PASSED_TO_BUYER =
    "HEADER_PASSED_TO_BUYER";
inline constexpr absl::string_view K_ANON_TOTAL_NUM_HASH =
    "K_ANON_TOTAL_NUM_HASH";
inline constexpr absl::string_view EXPECTED_K_ANON_TO_NON_K_ANON_RATIO =
    "EXPECTED_K_ANON_TO_NON_K_ANON_RATIO";
inline constexpr absl::string_view K_ANON_CLIENT_TIME_OUT_MS =
    "K_ANON_CLIENT_TIME_OUT_MS";
inline constexpr absl::string_view NUM_K_ANON_SHARDS = "NUM_K_ANON_SHARDS";
inline constexpr absl::string_view NUM_NON_K_ANON_SHARDS =
    "NUM_NON_K_ANON_SHARDS";
inline constexpr absl::string_view TEST_MODE_K_ANON_CACHE_TTL_MS =
    "TEST_MODE_K_ANON_CACHE_TTL_MS";
inline constexpr absl::string_view TEST_MODE_NON_K_ANON_CACHE_TTL_MS =
    "TEST_MODE_NON_K_ANON_CACHE_TTL_MS";
inline constexpr absl::string_view ENABLE_K_ANON_QUERY_CACHE =
    "ENABLE_K_ANON_QUERY_CACHE";
inline constexpr absl::string_view ENABLE_BUYER_CACHING =
    "ENABLE_BUYER_CACHING";

inline constexpr int kNumRuntimeFlags = 38;
inline constexpr std::array<absl::string_view, kNumRuntimeFlags> kFlags = {
    PORT,
    HEALTHCHECK_PORT,
    GET_BID_RPC_TIMEOUT_MS,
    KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS,
    SCORE_ADS_RPC_TIMEOUT_MS,
    SELLER_ORIGIN_DOMAIN,
    AUCTION_SERVER_HOST,
    GRPC_ARG_DEFAULT_AUTHORITY_VAL,
    KEY_VALUE_SIGNALS_HOST,
    TRUSTED_KEY_VALUE_V2_SIGNALS_HOST,
    BUYER_SERVER_HOSTS,
    ENABLE_BUYER_COMPRESSION,
    ENABLE_AUCTION_COMPRESSION,
    ENABLE_SELLER_FRONTEND_BENCHMARKING,
    CREATE_NEW_EVENT_ENGINE,
    SFE_INGRESS_TLS,
    SFE_TLS_KEY,
    SFE_TLS_CERT,
    AUCTION_EGRESS_TLS,
    BUYER_EGRESS_TLS,
    SFE_PUBLIC_KEYS_ENDPOINTS,
    SELLER_CLOUD_PLATFORMS_MAP,
    SFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND,
    SFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES,
    SELLER_CODE_FETCH_CONFIG,
    K_ANON_API_KEY,
    ALLOW_COMPRESSED_AUCTION_CONFIG,
    SCORING_SIGNALS_FETCH_MODE,
    HEADER_PASSED_TO_BUYER,
    K_ANON_TOTAL_NUM_HASH,
    EXPECTED_K_ANON_TO_NON_K_ANON_RATIO,
    K_ANON_CLIENT_TIME_OUT_MS,
    NUM_K_ANON_SHARDS,
    NUM_NON_K_ANON_SHARDS,
    TEST_MODE_K_ANON_CACHE_TTL_MS,
    TEST_MODE_NON_K_ANON_CACHE_TTL_MS,
    ENABLE_K_ANON_QUERY_CACHE,
    ENABLE_BUYER_CACHING};

inline std::vector<absl::string_view> GetServiceFlags() {
  std::vector<absl::string_view> flags(kFlags.begin(),
                                       kFlags.begin() + kNumRuntimeFlags);

  for (absl::string_view flag : kCommonServiceFlags) {
    flags.push_back(flag);
  }

  return flags;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_SELLER_FRONTEND_SERVICE_RUNTIME_FLAGS_H_
