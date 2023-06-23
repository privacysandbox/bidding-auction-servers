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

#include "services/common/loggers/build_input_process_response_benchmarking_logger.h"

#include "absl/strings/str_cat.h"

namespace privacy_sandbox::bidding_auction_servers {

void BuildInputProcessResponseBenchmarkingLogger::BuildInputBegin() {
  log_store_.push_back(absl::StrCat("Starting to build input at time [",
                                    FormatTime(absl::Now()), "]"));
  Begin();
  build_input_timer_.Start();
}

void BuildInputProcessResponseBenchmarkingLogger::BuildInputEnd() {
  build_input_timer_.End();
  log_store_.push_back(absl::StrCat(
      "Done building input, took [",
      std::to_string(
          ToInt64Microseconds(build_input_timer_.GetRuntimeDuration())),
      "] microseconds."));
  external_req_process_timer_.Start();
}

void BuildInputProcessResponseBenchmarkingLogger::HandleResponseBegin() {
  external_req_process_timer_.End();
  log_store_.push_back(
      absl::StrCat("External request processing finished, took [",
                   std::to_string(ToInt64Microseconds(
                       external_req_process_timer_.GetRuntimeDuration())),
                   "] microseconds."));
  log_store_.push_back(absl::StrCat("Starting to process response at time [",
                                    FormatTime(absl::Now()), "]"));
  handle_response_timer_.Start();
}

void BuildInputProcessResponseBenchmarkingLogger::HandleResponseEnd() {
  handle_response_timer_.End();
  log_store_.push_back(absl::StrCat(
      "Done processing response, took [",
      std::to_string(
          ToInt64Microseconds(handle_response_timer_.GetRuntimeDuration())),
      "] microseconds."));
  End();
}
}  // namespace privacy_sandbox::bidding_auction_servers
