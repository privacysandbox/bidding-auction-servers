/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_LOGGERS_SOURCE_LOCATION_CONTEXT_H_
#define SERVICES_COMMON_LOGGERS_SOURCE_LOCATION_CONTEXT_H_
#include <utility>

#include "src/util/status_macro/source_location.h"
namespace privacy_sandbox::bidding_auction_servers::log {
template <class T>
struct ParamWithSourceLoc {
  T mandatory_param;
  server_common::SourceLocation location;
  template <class U>
  ParamWithSourceLoc(
      U param, server_common::SourceLocation loc_in PS_LOC_CURRENT_DEFAULT_ARG)
      : mandatory_param(std::forward<U>(param)), location(loc_in) {}
};
}  // namespace privacy_sandbox::bidding_auction_servers::log

#endif  // SERVICES_COMMON_LOGGERS_SOURCE_LOCATION_CONTEXT_H_
