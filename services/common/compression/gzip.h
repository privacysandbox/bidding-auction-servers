/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_COMMON_CLIENTS_COMPRESSION_GZIP_H_
#define SERVICES_COMMON_CLIENTS_COMPRESSION_GZIP_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// As per the documentation in zlib.h, "Add 16 to windowBits to write a simple
// gzip header and trailer around the compressed data instead of a zlib
// wrapper." 15 is the default value, so add/OR 16 to it.
inline constexpr int kGzipWindowBits = 15 | 16;
// Default to 8 as per zlib.h's documentation.
inline constexpr int kDefaultMemLevel = 8;

// Compresses a string using gzip.
absl::StatusOr<std::string> GzipCompress(absl::string_view decompressed);

// Decompresses a gzip compressed string.
absl::StatusOr<std::string> GzipDecompress(absl::string_view compressed);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_COMPRESSION_GZIP_H_
