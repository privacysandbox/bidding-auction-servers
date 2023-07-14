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
inline constexpr char PORT[] = "AUCTION_PORT";
inline constexpr char JS_PATH[] = "JS_PATH";
inline constexpr char ENABLE_AUCTION_SERVICE_BENCHMARK[] =
    "ENABLE_AUCTION_SERVICE_BENCHMARK";
inline constexpr char ENABLE_SELLER_DEBUG_URL_GENERATION[] =
    "ENABLE_SELLER_DEBUG_URL_GENERATION";
inline constexpr char JS_URL[] = "JS_URL";
inline constexpr char JS_URL_FETCH_PERIOD_MS[] = "JS_URL_FETCH_PERIOD_MS";
inline constexpr char JS_TIME_OUT_MS[] = "JS_TIME_OUT_MS";
inline constexpr char ENABLE_SELLER_CODE_WRAPPER[] =
    "ENABLE_SELLER_CODE_WRAPPER";
inline constexpr char ENABLE_ADTECH_CODE_LOGGING[] =
    "ENABLE_ADTECH_CODE_LOGGING";
inline constexpr char ENABLE_REPORT_RESULT_URL_GENERATION[] =
    "ENABLE_REPORT_RESULT_URL_GENERATION";
inline constexpr char ENABLE_REPORT_WIN_URL_GENERATION[] =
    "ENABLE_REPORT_WIN_URL_GENERATION";

inline constexpr absl::string_view kFlags[] = {
    PORT,
    JS_PATH,
    ENABLE_AUCTION_SERVICE_BENCHMARK,
    ENABLE_SELLER_DEBUG_URL_GENERATION,
    JS_URL,
    JS_URL_FETCH_PERIOD_MS,
    JS_TIME_OUT_MS,
    ENABLE_SELLER_CODE_WRAPPER,
    ENABLE_ADTECH_CODE_LOGGING,
    ENABLE_REPORT_RESULT_URL_GENERATION,
    ENABLE_REPORT_WIN_URL_GENERATION};

inline std::vector<absl::string_view> GetServiceFlags() {
  int size = sizeof(kFlags) / sizeof(kFlags[0]);
  std::vector<absl::string_view> flags(kFlags, kFlags + size);

  for (absl::string_view flag : kCommonServiceFlags) {
    flags.push_back(flag);
  }

  return flags;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_AUCTION_SERVICE_RUNTIME_FLAGS_H_
