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

#include "services/common/compression/gzip.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>

#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::boost::iostreams::copy;
using ::boost::iostreams::filtering_istreambuf;
using ::boost::iostreams::gzip_compressor;
using ::boost::iostreams::gzip_decompressor;
using ::boost::iostreams::gzip_params;
using ::boost::iostreams::gzip::best_compression;

std::string BoostCompress(absl::string_view decompressed_string) {
  std::istringstream origin(decompressed_string.data());

  filtering_istreambuf buffer;
  buffer.push(gzip_compressor(gzip_params(best_compression)));
  buffer.push(origin);

  std::ostringstream compressed_string;
  copy(buffer, compressed_string);
  return compressed_string.str();
}

std::string BoostDecompress(const std::string& compressed_string) {
  std::istringstream compressed(compressed_string);

  filtering_istreambuf buffer;
  buffer.push(gzip_decompressor());
  buffer.push(compressed);

  std::ostringstream decompressed_string;
  copy(buffer, decompressed_string);
  return decompressed_string.str();
}

std::string GeneratePayload(int size_in_mb) {
  int size_in_bytes = size_in_mb * 1024 * 1024;
  return std::string(size_in_bytes, 'Q');
}

TEST(GzipCompressionTests, CompressDecompress_EndToEnd) {
  std::string payload = GeneratePayload(100);
  absl::StatusOr<std::string> compressed = GzipCompress(payload);
  ASSERT_TRUE(compressed.ok()) << compressed.status();

  absl::StatusOr<std::string> decompressed = GzipDecompress(*compressed);
  ASSERT_TRUE(decompressed.ok()) << decompressed.status();

  ASSERT_EQ(payload, decompressed.value());
}

TEST(GzipCompressionTests, CompressWithBoost) {
  // Verify that a gzip compressed string from another library can also be
  // decompressed successfully using zlib's decompressor.
  std::string payload = "hello";
  std::string compressed = BoostCompress(payload);

  absl::StatusOr<std::string> decompressed = GzipDecompress(compressed);
  ASSERT_EQ(payload, decompressed.value());
}

TEST(GzipCompressionTests, DecompressWithBoost) {
  // Verify a string compressed using zlib can be decompressed by another
  // library as well (Boost).
  std::string payload = "hello";
  absl::StatusOr<std::string> compressed = GzipCompress(payload);

  std::string boost_decompress = BoostDecompress(*compressed);
  ASSERT_EQ(payload, boost_decompress);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
