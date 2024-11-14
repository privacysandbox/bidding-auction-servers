/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_DATA_VERSION_UTIL_H_
#define SERVICES_COMMON_DATA_VERSION_UTIL_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace privacy_sandbox::bidding_auction_servers {
inline constexpr char kMissingBucketName[] = "Missing bucket name.";
inline constexpr char kMissingBlobName[] = "Missing blob name.";

inline absl::StatusOr<std::string> GetBucketBlobVersion(
    absl::string_view bucket_name, absl::string_view blob_name) {
  if (bucket_name.empty()) {
    return absl::InvalidArgumentError(kMissingBucketName);
  }
  if (blob_name.empty()) {
    return absl::InvalidArgumentError(kMissingBlobName);
  }
  return absl::StrCat(bucket_name, "/", blob_name);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_DATA_VERSION_UTIL_H_
