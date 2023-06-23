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

#include "services/common/loggers/timer.h"

#include "absl/time/clock.h"

namespace privacy_sandbox::bidding_auction_servers {

void Timer::Start() {
  start_time_ = absl::Now();
  running_ = true;
}

void Timer::End() {
  end_time_ = absl::Now();
  running_ = false;
}

[[nodiscard]] absl::Duration Timer::GetRuntimeDuration() const {
  if (running_) {
    return absl::Now() - start_time_;
  } else {
    return end_time_ - start_time_;
  }
}
}  // namespace privacy_sandbox::bidding_auction_servers
