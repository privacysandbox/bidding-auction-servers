// Copyright 2024 Google LLC
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

#include "services/seller_frontend_service/util/cbor_common_util.h"

#include <string>
#include <utility>

#include "absl/numeric/bits.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "services/common/compression/gzip.h"
#include "src/util/status_macro/status_macros.h"

#include "cbor.h"
namespace privacy_sandbox::bidding_auction_servers {

cbor_item_t* cbor_build_int(int input) {
  if (input < 0) {
    return cbor_build_negint(static_cast<uint>(-input - 1));
  }
  return cbor_build_uint(input);
}

cbor_item_t* cbor_build_uint(uint input) {
  if (input <= std::numeric_limits<uint8_t>::max()) {
    return cbor_build_uint8(input);
  } else if (input <= std::numeric_limits<uint16_t>::max()) {
    return cbor_build_uint16(input);
  } else if (input <= std::numeric_limits<uint32_t>::max()) {
    return cbor_build_uint32(input);
  }
  return cbor_build_uint64(input);
}

cbor_item_t* cbor_build_negint(uint input) {
  if (input <= std::numeric_limits<uint8_t>::max()) {
    return cbor_build_negint8(input);
  } else if (input <= std::numeric_limits<uint16_t>::max()) {
    return cbor_build_negint16(input);
  } else if (input <= std::numeric_limits<uint32_t>::max()) {
    return cbor_build_negint32(input);
  }
  return cbor_build_negint64(input);
}

absl::StatusOr<cbor_item_t*> cbor_build_float(double input) {
  PS_ASSIGN_OR_RETURN(double decoded_double,
                      EncodeDecodeFloatCbor(input, CBOR_FLOAT_64));
  PS_ASSIGN_OR_RETURN(float decoded_single,
                      EncodeDecodeFloatCbor(input, CBOR_FLOAT_32));
  if (AreFloatsEqual(decoded_double, decoded_single)) {
    PS_ASSIGN_OR_RETURN(float decoded_half,
                        EncodeDecodeFloatCbor(input, CBOR_FLOAT_16));
    if (AreFloatsEqual(decoded_single, decoded_half)) {
      PS_VLOG(5) << "Original input: " << input
                 << ", encoded as half precision float: " << decoded_half;
      return cbor_build_float2(input);
    }
    PS_VLOG(5) << "Original input: " << input
               << ", encoded as single precision float: " << decoded_single;
    return cbor_build_float4(input);
  }
  PS_VLOG(5) << "Original input: " << input
             << ", encoded as double precision float: " << decoded_double;
  return cbor_build_float8(input);
}

absl::StatusOr<std::string> SerializeToCbor(cbor_item_t& cbor_data_root) {
  // Serialize the payload to CBOR.
  const size_t cbor_serialized_data_size =
      cbor_serialized_size(&cbor_data_root);
  if (!cbor_serialized_data_size) {
    return absl::InternalError("CBOR data size too large");
  }

  std::string byte_string;
  byte_string.resize(cbor_serialized_data_size);
  if (cbor_serialize(&cbor_data_root,
                     reinterpret_cast<unsigned char*>(byte_string.data()),
                     cbor_serialized_data_size) == 0) {
    return absl::InternalError("Failed to serialize data to CBOR");
  }
  return byte_string;
}

absl::Status CborSerializeByteString(absl::string_view key,
                                     const std::string& value,
                                     ErrorHandler error_handler,
                                     cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_bytestring(
          ReinterpretConstCharPtrAsUnsignedPtr(value.data()), value.size()))};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeFloat(absl::string_view key, double value,
                                ErrorHandler error_handler, cbor_item_t& root) {
  PS_ASSIGN_OR_RETURN(cbor_item_t * float_val, cbor_build_float(value));
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(float_val)};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeBool(absl::string_view key, bool value,
                               ErrorHandler error_handler, cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_bool(value))};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeInt(absl::string_view key, int value,
                              ErrorHandler error_handler, cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_int(value))};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeString(absl::string_view key, absl::string_view value,
                                 ErrorHandler error_handler,
                                 cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_stringn(value.data(), value.size()))};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers
