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

#include "services/common/telemetry/telemetry_flag.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/text_format.h"

namespace privacy_sandbox::server_common {
namespace {

template <typename T>
inline absl::StatusOr<T> ParseText(absl::string_view text) {
  T message;
  if (!google::protobuf::TextFormat::ParseFromString(text.data(), &message)) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid proto format:{", text, "}"));
  }
  return message;
}

}  // namespace

bool AbslParseFlag(absl::string_view text, TelemetryFlag* flag,
                   std::string* err) {
  absl::StatusOr<TelemetryConfig> s = ParseText<TelemetryConfig>(text);
  if (!s.ok()) {
    *err = s.status().message();
    return false;
  }
  flag->server_config = *s;
  return true;
}

std::string AbslUnparseFlag(const TelemetryFlag& flag) {
  return flag.server_config.ShortDebugString();
}

}  // namespace privacy_sandbox::server_common
