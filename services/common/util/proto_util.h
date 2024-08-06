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

#ifndef SERVICES_COMMON_UTIL_PROTO_UTIL_H_
#define SERVICES_COMMON_UTIL_PROTO_UTIL_H_

#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>

#include "absl/status/statusor.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

// Converts the provided JSON string to a protobuf struct value.
inline absl::StatusOr<google::protobuf::Value> JsonStringToValue(
    absl::string_view json_string) {
  google::protobuf::Value val;
  PS_RETURN_IF_ERROR(
      google::protobuf::util::JsonStringToMessage(json_string, &val));
  return val;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_PROTO_UTIL_H_
