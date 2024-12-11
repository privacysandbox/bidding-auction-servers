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

#include "services/common/util/event.h"

#include <utility>

namespace privacy_sandbox::bidding_auction_servers {

Event::Event(struct event_base* base, evutil_socket_t fd, short event_type,
             Event::Callback event_callback, void* arg, int priority,
             struct timeval* event_timeout, OnDelete on_delete)
    : priority_(priority),
      event_(event_new(base, fd, event_type, event_callback, arg)),
      on_delete_(std::move(on_delete)) {
  event_priority_set(event_, priority_);
  event_add(event_, event_timeout);
}

struct event* Event::get() { return event_; }
Event::~Event() {
  if (event_) {
    if (on_delete_) {
      on_delete_(event_);
    }
    event_del(event_);
    event_free(event_);
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
