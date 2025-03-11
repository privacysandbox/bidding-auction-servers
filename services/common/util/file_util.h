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

#ifndef SERVICES_COMMON_UTIL_FILE_UTIL_H_
#define SERVICES_COMMON_UTIL_FILE_UTIL_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kPathFailed[] = "Failed to load file from path: ";

// Gets the contents of the provided path.
absl::StatusOr<std::string> GetFileContent(absl::string_view path,
                                           bool log_on_error = false);

// Writes the provided contents to the file path.
absl::Status WriteToFile(absl::string_view path, absl::string_view contents,
                         bool log_on_error = false);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_FILE_UTIL_H_
