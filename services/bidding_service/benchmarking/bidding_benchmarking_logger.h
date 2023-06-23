//   Copyright 2022 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#ifndef SERVICES_COMMON_LOGGERS_BIDDING_BENCHMARKING_LOGGER_H_
#define SERVICES_COMMON_LOGGERS_BIDDING_BENCHMARKING_LOGGER_H_

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/clock.h"
#include "services/common/loggers/build_input_process_response_benchmarking_logger.h"
#include "services/common/loggers/timer.h"

namespace privacy_sandbox::bidding_auction_servers {
class BiddingBenchmarkingLogger
    : public BuildInputProcessResponseBenchmarkingLogger {
 public:
  BiddingBenchmarkingLogger() = default;
  explicit BiddingBenchmarkingLogger(std::string_view request_id)
      : BuildInputProcessResponseBenchmarkingLogger(request_id) {}

  // BiddingBenchmarkingLogger is neither copyable nor movable.
  BiddingBenchmarkingLogger(const BiddingBenchmarkingLogger&) = delete;
  BiddingBenchmarkingLogger& operator=(const BiddingBenchmarkingLogger&) =
      delete;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_LOGGERS_BIDDING_BENCHMARKING_LOGGER_H_
