// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/compression/compression_utils.h"

#include <utility>

#include "absl/strings/str_cat.h"
#include "services/common/compression/gzip.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<CompressionType> ToCompressionType(int num) {
  switch (num) {
    case 0:
      return CompressionType::kUncompressed;
    case 1:
      return CompressionType::kGzip;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Cannot convert value to CompressionType enum: ", num));
  }
}

absl::StatusOr<std::string> Compress(std::string uncompressed,
                                     CompressionType compression_type) {
  switch (compression_type) {
    case kUncompressed:
      return std::move(uncompressed);
    case kGzip:
      return GzipCompress(uncompressed);
    default:
      return absl::InvalidArgumentError(
          "Invalid compression type supplied during compression");
  }
}

absl::StatusOr<std::string> Decompress(std::string compressed,
                                       CompressionType compression_type) {
  switch (compression_type) {
    case kUncompressed:
      return std::move(compressed);
    case kGzip:
      return GzipDecompress(compressed);
    default:
      return absl::InvalidArgumentError(
          "Invalid compression type supplied during decompression");
  }
}

absl::StatusOr<std::string> Decompress(CompressionType compression_type,
                                       absl::string_view compressed) {
  switch (compression_type) {
    case kGzip:
      return GzipDecompress(compressed);
    default:
      return absl::InvalidArgumentError(
          "Invalid compression type supplied during decompression");
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
