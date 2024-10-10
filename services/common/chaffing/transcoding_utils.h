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

#ifndef SERVICES_COMMON_CHAFFING_TRANSCODING_UTILS_H_
#define SERVICES_COMMON_CHAFFING_TRANSCODING_UTILS_H_

#include <algorithm>
#include <string>
#include <utility>

#include "api/bidding_auction_servers.pb.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "services/common/compression/compression_utils.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr int kVersionAndCompressionTypeSizeBytes = 1;
inline constexpr int kDataSizeBytes = 4;
inline constexpr int kTotalMetadataSizeBytes =
    kVersionAndCompressionTypeSizeBytes + kDataSizeBytes;

inline constexpr uint8_t kCompressionTypeMask = 0b00001111;
inline constexpr uint8_t kPayloadVersionMask = 0b11110000;

template <typename GetBidsProto>
struct DecodedGetBidsPayload {
  uint8_t version;
  CompressionType compression_type;
  uint32_t payload_length;
  GetBidsProto get_bids_proto;
};

// Encodes a GetBids request payload in following format:
// - 1 byte containing:
//   - 4 bits for the framing version (the format/structure of the payload)
//   - 4 bits for the compression algorithm used to compress the payload
// - 4 bytes for the size of the data
// - X bytes of data (e.g. the payload)
// - Y bytes of padding
template <typename GetBidsProto>
absl::StatusOr<std::string> EncodeAndCompressGetBidsPayload(
    const GetBidsProto& raw_proto, CompressionType compression_type,
    size_t padded_request_size = 0) {
  const bool is_get_bids_proto =
      std::is_base_of<GetBidsRequest::GetBidsRawRequest, GetBidsProto>::value ||
      std::is_base_of<GetBidsResponse::GetBidsRawResponse, GetBidsProto>::value;
  static_assert(
      is_get_bids_proto,
      "raw_proto should be either a GetBids RawRequest or RawResponse");

  int payload_size = kTotalMetadataSizeBytes;
  PS_ASSIGN_OR_RETURN(
      std::string compressed_payload,
      Compress(raw_proto.SerializeAsString(), compression_type));
  payload_size += std::max(compressed_payload.length(), padded_request_size);

  // Create backing array for QuicheDataWriter and initialize the writer with
  // it.
  const int kEncodedDataSize = payload_size;
  char encoded_payload[kEncodedDataSize];
  quiche::QuicheDataWriter writer(kEncodedDataSize, encoded_payload);

  // Write 1 byte for version and compression algorithm.
  int8_t framing_and_compression_byte = compression_type;
  writer.WriteUInt8(framing_and_compression_byte);
  // Write the length of the payload using 4 bytes.
  writer.WriteUInt32(compressed_payload.length());
  // Write the actual payload.
  writer.WriteStringPiece(compressed_payload);
  // Fill the rest of the array with padding.
  writer.WritePadding();

  PS_VLOG(8) << "Payload successfully encoded...";
  return std::string(encoded_payload, kEncodedDataSize);
}

template <typename GetBidsProto>
absl::StatusOr<DecodedGetBidsPayload<GetBidsProto>> DecodeGetBidsPayload(
    absl::string_view encoded_payload) {
  const bool is_get_bids_proto =
      std::is_base_of<GetBidsRequest::GetBidsRawRequest, GetBidsProto>::value ||
      std::is_base_of<GetBidsResponse::GetBidsRawResponse, GetBidsProto>::value;
  static_assert(
      is_get_bids_proto,
      "raw_proto should be either a GetBids RawRequest or RawResponse");

  quiche::QuicheDataReader reader(encoded_payload);
  uint8_t first_byte;
  if (!reader.ReadUInt8(&first_byte)) {
    return absl::InvalidArgumentError("Cannot read version/compression byte");
  }

  uint32_t payload_length;
  if (!reader.ReadUInt32(&payload_length)) {
    return absl::InvalidArgumentError("Cannot read payload length bytes");
  }

  absl::string_view payload;
  if (!reader.ReadStringPiece(&payload, payload_length)) {
    return absl::InvalidArgumentError("Cannot read payload");
  }

  PS_ASSIGN_OR_RETURN(CompressionType compression_type,
                      ToCompressionType(first_byte & kCompressionTypeMask));

  GetBidsProto get_bids_proto;
  if (compression_type != CompressionType::kUncompressed) {
    PS_ASSIGN_OR_RETURN(std::string decompressed,
                        Decompress(compression_type, payload));
    if (!get_bids_proto.ParseFromString(decompressed)) {
      return absl::InvalidArgumentError(
          "Cannot parse proto from GetBids payload");
    }
  } else {
    if (!get_bids_proto.ParseFromArray(payload.data(), payload.length())) {
      return absl::InvalidArgumentError(
          "Cannot parse proto from GetBids payload");
    }
  }

  uint8_t version = first_byte & kPayloadVersionMask;
  DecodedGetBidsPayload<GetBidsProto> decoded_payload = {
      .version = version,
      .compression_type = compression_type,
      .payload_length = payload_length,
      .get_bids_proto = std::move(get_bids_proto)};

  return decoded_payload;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CHAFFING_TRANSCODING_UTILS_H_
