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

#ifndef SERVICES_COMMON_UTIL_EVENT_H_
#define SERVICES_COMMON_UTIL_EVENT_H_

#include <functional>

#include "event2/event.h"
#include "event2/event_struct.h"
#include "services/common/util/constants.h"

namespace privacy_sandbox::bidding_auction_servers {

// Wraps the event used by libevent. This wrapper makes it easier to manage
// lifecycle of the underlying event.
class Event {
 public:
  using OnDelete = std::function<void(struct event*)>;
  // Arguments are documented here:
  // https://libevent.org/doc/event_8h.html#aed2307f3d9b38e07cc10c2607322d758
  using Callback = void (*)(/*fd or signal=*/int, /*events=*/short,
                            /*pointer to user provided data=*/void*);

  explicit Event(struct event_base* base, evutil_socket_t fd, short event_type,
                 Callback event_callback, void* arg,
                 int priority = kNumEventPriorities / 2,
                 struct timeval* event_timeout = nullptr,
                 OnDelete on_delete = nullptr);
  struct event* get();
  virtual ~Event();

 private:
  int priority_;
  struct event* event_ = nullptr;
  OnDelete on_delete_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_EVENT_H_
