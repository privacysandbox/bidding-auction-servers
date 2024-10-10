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

#ifndef FLEDGE_SERVICES_AUCTION_SERVICE_RUNTIME_FLAGS_H_
#define FLEDGE_SERVICES_AUCTION_SERVICE_RUNTIME_FLAGS_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "services/common/constants/common_service_flags.h"

namespace privacy_sandbox::bidding_auction_servers {

// Define runtime flag names.
inline constexpr absl::string_view PORT = "AUCTION_PORT";
inline constexpr absl::string_view HEALTHCHECK_PORT =
    "AUCTION_HEALTHCHECK_PORT";
inline constexpr absl::string_view ENABLE_AUCTION_SERVICE_BENCHMARK =
    "ENABLE_AUCTION_SERVICE_BENCHMARK";
inline constexpr absl::string_view SELLER_CODE_FETCH_CONFIG =
    "SELLER_CODE_FETCH_CONFIG";
inline constexpr absl::string_view UDF_NUM_WORKERS = "UDF_NUM_WORKERS";
inline constexpr absl::string_view JS_WORKER_QUEUE_LEN = "JS_WORKER_QUEUE_LEN";
inline constexpr absl::string_view ENABLE_REPORT_WIN_INPUT_NOISING =
    "ENABLE_REPORT_WIN_INPUT_NOISING";
inline constexpr char
    AUCTION_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND[] =
        "AUCTION_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND";
inline constexpr absl::string_view
    AUCTION_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES =
        "AUCTION_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES";

inline constexpr int kNumRuntimeFlags = 9;
inline constexpr std::array<absl::string_view, kNumRuntimeFlags> kFlags = {
    PORT,
    HEALTHCHECK_PORT,
    ENABLE_AUCTION_SERVICE_BENCHMARK,
    SELLER_CODE_FETCH_CONFIG,
    UDF_NUM_WORKERS,
    JS_WORKER_QUEUE_LEN,
    ENABLE_REPORT_WIN_INPUT_NOISING,
    AUCTION_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND,
    AUCTION_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES,
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

#endif  // FLEDGE_SERVICES_AUCTION_SERVICE_RUNTIME_FLAGS_H_
