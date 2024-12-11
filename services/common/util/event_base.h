//  Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_EVENT_BASE_H_
#define SERVICES_COMMON_UTIL_EVENT_BASE_H_

#include "event2/event.h"
#include "event2/thread.h"
#include "services/common/util/constants.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

// Wrapper for the libevent structure to hold information and state for a
// libevent dispatch loop.
class EventBase {
 public:
  explicit EventBase(bool use_pthreads = true,
                     int num_priorities = kNumEventPriorities);
  virtual ~EventBase();

  // Gets the underlying event base data type.
  struct event_base* get();

 private:
  struct event_base* event_base_ = nullptr;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_EVENT_BASE_H_
