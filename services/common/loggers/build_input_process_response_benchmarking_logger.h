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

#ifndef SERVICES_COMMON_LOGGERS_BUILD_INPUT_PROCESS_RESPONSE_BENCHMARKING_LOGGER_H_
#define SERVICES_COMMON_LOGGERS_BUILD_INPUT_PROCESS_RESPONSE_BENCHMARKING_LOGGER_H_

#include "services/common/loggers/benchmarking_logger.h"
namespace privacy_sandbox::bidding_auction_servers {

/**
 * Logger for benchmarking any service for which the usage pattern is:
 * 1. Build a request
 * 2. Send out the request to be processed
 * 3. Process the response
 * Should be included as a class member for a class being benchmarked. Reports
 * time in Microseconds.
 */
class BuildInputProcessResponseBenchmarkingLogger : public BenchmarkingLogger {
 public:
  BuildInputProcessResponseBenchmarkingLogger() = default;
  explicit BuildInputProcessResponseBenchmarkingLogger(
      std::string_view request_id)
      : BenchmarkingLogger(request_id) {}

  // BuildInputProcessResponseBenchmarkingLogger is neither copyable nor
  // movable.
  BuildInputProcessResponseBenchmarkingLogger(
      const BuildInputProcessResponseBenchmarkingLogger&) = delete;
  BuildInputProcessResponseBenchmarkingLogger& operator=(
      const BuildInputProcessResponseBenchmarkingLogger&) = delete;

  /**
   * Starts the Input Building Timer and logs the start time.
   */
  virtual void BuildInputBegin();

  /**
   * Stops the Input Building Timer, logs its runtime, and starts the External
   * Request Processing timer.
   */
  virtual void BuildInputEnd();

  /**
   * Stops the External Request Processing Timer, logs its runtime, starts the
   * Response Handling Timer, and logs its start time.
   */
  virtual void HandleResponseBegin();

  /**
   * Stops the Response Handling Timer, logs its runtime, and calls Finish() to
   * print the logs.
   */
  virtual void HandleResponseEnd();

 private:
  Timer build_input_timer_;
  Timer external_req_process_timer_;
  Timer handle_response_timer_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_LOGGERS_BUILD_INPUT_PROCESS_RESPONSE_BENCHMARKING_LOGGER_H_
