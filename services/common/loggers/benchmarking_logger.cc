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

#include "services/common/loggers/benchmarking_logger.h"

#include <cstring>
#include <iostream>

#include "absl/strings/str_cat.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

void BenchmarkingLogger::Begin() {
  log_store_.push_back(absl::StrCat("Starting overall request at time [",
                                    FormatTime(absl::Now()), "]"));
  overall_timer_.Start();
}

void BenchmarkingLogger::End() {
  overall_timer_.End();
  log_store_.push_back(absl::StrCat(
      "Done with overall request, took [",
      std::to_string(ToInt64Microseconds(overall_timer_.GetRuntimeDuration())),
      "] microseconds."));
}

BenchmarkingLogger::~BenchmarkingLogger() {
  for (const std::string& log_record : log_store_) {
    PS_VLOG(kNoisyInfo) << absl::StrCat("\nBenchmarking for request ID [",
                                        request_id_, "]: ", log_record);
  }
  PS_VLOG(kNoisyInfo) << "\n\n";
}
}  // namespace privacy_sandbox::bidding_auction_servers
