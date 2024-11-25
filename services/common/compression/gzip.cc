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

#include <zlib.h>

#include "absl/strings/str_format.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::string> GzipCompress(absl::string_view uncompressed) {
  z_stream zs;
  zs.zalloc = Z_NULL;
  zs.zfree = Z_NULL;
  zs.opaque = Z_NULL;
  zs.avail_in = (uInt)uncompressed.size();
  zs.next_in = (Bytef*)uncompressed.data();

  int deflate_init_status =
      deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, kGzipWindowBits | 16,
                   kDefaultMemLevel, Z_DEFAULT_STRATEGY);
  if (deflate_init_status != Z_OK) {
    return absl::InternalError(
        absl::StrFormat("Error initializing data for gzip compression (deflate "
                        "init status: %d)",
                        deflate_init_status));
  }

  const int kPartitionSizeBound = deflateBound(&zs, uncompressed.size());
  std::string partition_output_buffer(kPartitionSizeBound, '\0');

  zs.avail_out = (uInt)kPartitionSizeBound;
  // We'll manually write the size of the compressed data manually after
  // compressing the data.
  zs.next_out = (Bytef*)partition_output_buffer.data();

  const int deflate_status = deflate(&zs, Z_FINISH);
  if (deflate_status != Z_STREAM_END) {
    deflateEnd(&zs);
    return absl::InternalError(absl::StrFormat(
        "Error compressing data using gzip (deflate status: %d)",
        deflate_status));
  }

  // Free all memory held by the z_stream object.
  const int deflate_end_status = deflateEnd(&zs);
  if (deflate_end_status != Z_OK) {
    return absl::InternalError(absl::StrFormat(
        "Error closing compression data stream (deflate end status: %d)",
        deflate_end_status));
  }

  partition_output_buffer.resize(zs.total_out);
  return partition_output_buffer;
}

absl::StatusOr<std::string> GzipDecompress(absl::string_view compressed) {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  zs.next_in = (Bytef*)compressed.data();
  zs.avail_in = compressed.size();

  const int inflate_init_status = inflateInit2(&zs, kGzipWindowBits | 16);
  if (inflate_init_status != Z_OK) {
    return absl::InternalError(
        absl::StrFormat("Error during gzip decompression initialization: "
                        "(inflate init status: %d)",
                        inflate_init_status));
  }

  char output_buffer[32768];  // 32 KiB chunks.
  std::string decompressed;

  int inflate_status;
  do {
    zs.next_out = reinterpret_cast<Bytef*>(output_buffer);
    zs.avail_out = sizeof(output_buffer);

    inflate_status = inflate(&zs, Z_NO_FLUSH);
    // Copy the decompressed output from the buffer to our result string.
    if (decompressed.size() < zs.total_out) {
      decompressed.append(output_buffer, zs.total_out - decompressed.size());
    }
  } while (inflate_status == Z_OK);

  if (inflate_status != Z_STREAM_END) {
    inflateEnd(&zs);
    return absl::DataLossError(absl::StrFormat(
        "Exception during gzip decompression: (inflate status: %d)",
        inflate_status));
  }

  const int inflate_end_status = inflateEnd(&zs);
  if (inflate_end_status != Z_OK) {
    return absl::InternalError(absl::StrFormat(
        "Error closing compression data stream (inflate end status: %d(",
        inflate_end_status));
  }

  return decompressed;
}

}  // namespace privacy_sandbox::bidding_auction_servers
