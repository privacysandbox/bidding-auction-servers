// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SERVICES_COMMON_TEST_UTILS_PROTO_UTILS_H_
#define SERVICES_COMMON_TEST_UTILS_PROTO_UTILS_H_

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/text_format.h"

namespace privacy_sandbox::bidding_auction_servers {

template <typename T>
T ParseTextOrDie(absl::string_view text) {
  T message;
  CHECK(google::protobuf::TextFormat::ParseFromString(text.data(), &message));
  return message;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_UTILS_PROTO_UTILS_H_
