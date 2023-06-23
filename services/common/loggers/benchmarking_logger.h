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

#ifndef SERVICES_COMMON_LOGGERS_BENCHMARKING_LOGGER_H_
#define SERVICES_COMMON_LOGGERS_BENCHMARKING_LOGGER_H_

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "absl/time/clock.h"
#include "services/common/loggers/timer.h"

namespace privacy_sandbox::bidding_auction_servers {

/**
 * Logger for benchmarking Auction Service and possibly Bidding Service -
 * basically any piece of code for which the usage pattern is:
 * 1. Build a request
 * 2. Send out the request to be processed
 * 3. Process the response
 * Should be included as a class member for a class being benchmarked. Reports
 * time in Microseconds.
 */
class BenchmarkingLogger {
 public:
  /**
   * Creates a new BenchmarkingLogger.
   * request_id An arbitrary unique string identifier, allowing a single
   * request to be tracked through a potentially huge amount of logs. A precise
   * timestamp is an option, or if the request itself has an ID that can be
   * used.
   */
  explicit BenchmarkingLogger(std::string_view request_id)
      : request_id_(request_id) {}
  BenchmarkingLogger() : request_id_("no request id provided") {}
  virtual ~BenchmarkingLogger();

  /**
   * Starts the Overall Timer and logs the start time.
   */
  virtual void Begin();

  /**
   * Stops the Overall Timer and logs its runtime. Then prints all logs.
   */
  virtual void End();

 private:
  Timer overall_timer_;

 protected:
  std::vector<std::string> log_store_;
  std::string request_id_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_LOGGERS_BENCHMARKING_LOGGER_H_
