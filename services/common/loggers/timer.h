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

#ifndef SERVICES_COMMON_LOGGERS_TIMER_H_
#define SERVICES_COMMON_LOGGERS_TIMER_H_

#include "absl/time/time.h"

namespace privacy_sandbox::bidding_auction_servers {

/**
 * Basic wall-clock style timer (i.e. does not account for context
 * switches).
 */
class Timer {
 public:
  Timer() {}
  virtual ~Timer() {}

  /**
   * Starts the Timer.
   */
  void Start();

  /**
   * Ends the Timer.
   */
  void End();

  /**
   * Gets duration from start to end, if the Timer is stopped. If the Timer
   * is still running, duration since start.
   */
  [[nodiscard]] absl::Duration GetRuntimeDuration() const;

  /**
   * Gets the start_time_.
   */
  [[nodiscard]] absl::Time GetStartTime() const { return start_time_; }

  /**
   * Gets the end_time_.
   */
  [[nodiscard]] absl::Time GetEndTime() const { return end_time_; }

 private:
  bool running_ = false;
  absl::Time start_time_;
  absl::Time end_time_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_LOGGERS_TIMER_H_
