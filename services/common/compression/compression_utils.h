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

#ifndef SERVICES_COMMON_CLIENTS_COMPRESSION_UTILS_H_
#define SERVICES_COMMON_CLIENTS_COMPRESSION_UTILS_H_

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

enum CompressionType : std::uint8_t { kUncompressed = 0, kGzip = 1 };

absl::StatusOr<CompressionType> ToCompressionType(int num);

// Compresses std::string using the specified compression algorithm.
absl::StatusOr<std::string> Compress(std::string uncompressed,
                                     CompressionType compression_type);

// Two versions of Decompress() are provided below.
// Some callers have std::strings, some have absl::string_view.
// absl::string_view signatures are provided so callers with string_views don't
// have to create a copy of their string to compress/decompress. The reversed
// ordering of the parameters is to clearly differentiate which signature a
// client is calling.

// The signatures accepting absl::string_views cannot be called with
// CompressionType::kUncompressed because the methods cannot create an
// std::string from an absl::string_view *without creating* a copy of the input
// string.

// Decompresses std::string using the specified compression algorithm.
absl::StatusOr<std::string> Decompress(std::string compressed,
                                       CompressionType compression_type);
// Decompresses absl::string_view. This method *cannot* be called with
// compression_type = kUncompressed.
absl::StatusOr<std::string> Decompress(CompressionType compression_type,
                                       absl::string_view compressed);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_COMPRESSION_UTILS_H_
