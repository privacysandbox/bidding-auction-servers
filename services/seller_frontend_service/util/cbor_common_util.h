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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CBOR_COMMON_UTIL_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CBOR_COMMON_UTIL_H_

#include <limits>
#include <string>

#include "absl/status/statusor.h"
#include "services/common/util/data_util.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/scoped_cbor.h"
#include "src/util/status_macro/status_macros.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
using ErrorHandler = const std::function<void(const grpc::Status&)>&;

// Serializes cbor_item_t to byte string.
absl::StatusOr<std::string> SerializeToCbor(cbor_item_t& cbor_data_root);

// Encodes a floating-point value to CBOR format with a specified precision,
// decodes it back, and returns the decoded value as a double.
template <typename T>
absl::StatusOr<double> EncodeDecodeFloatCbor(T input, cbor_float_width width) {
  std::string encoded;
  switch (width) {
    case CBOR_FLOAT_16: {
      ScopedCbor candidate(cbor_build_float2(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
    case CBOR_FLOAT_32: {
      ScopedCbor candidate(cbor_build_float4(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
    default: {
      ScopedCbor candidate(cbor_build_float8(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
  }

  cbor_load_result cbor_result;
  ScopedCbor loaded_data(
      cbor_load(reinterpret_cast<unsigned char*>(encoded.data()),
                encoded.size(), &cbor_result));
  if (*loaded_data == nullptr || cbor_result.error.code != CBOR_ERR_NONE) {
    return absl::InternalError("Failed to load CBOR encoded float");
  }

  return cbor_float_get_float(*loaded_data);
}

// Checks for floats equality (subject to the system's limits).
template <typename T, typename U>
bool AreFloatsEqual(T a, U b) {
  bool result = std::fabs(a - b) < std::numeric_limits<double>::epsilon();
  PS_VLOG(6) << "a: " << a << ", b: " << b << ", diff: " << fabs(a - b)
             << ", EPS: " << std::numeric_limits<double>::epsilon()
             << ", floats equal: " << result;
  return result;
}

inline unsigned char* ReinterpretConstCharPtrAsUnsignedPtr(const char* ptr) {
  return const_cast<unsigned char*>(
      reinterpret_cast<const unsigned char*>(ptr));
}

// Decodes cbor string input to std::string
inline std::string CborDecodeString(const cbor_item_t* input) {
  return std::string(reinterpret_cast<char*>(cbor_string_handle(input)),
                     cbor_string_length(input));
}

// Decodes cbor string input to std::string after validating that
// the type of the item is a string
inline absl::StatusOr<std::string> CheckTypeAndCborDecodeString(
    const cbor_item_t* input) {
  if (!cbor_isa_string(input)) {
    return absl::InvalidArgumentError("cbor_item is not a string");
  }
  return CborDecodeString(input);
}

// Decodes cbor byte string input to std::string
inline std::string CborDecodeByteString(cbor_item_t* input) {
  return std::string(reinterpret_cast<char*>(cbor_bytestring_handle(input)),
                     cbor_bytestring_length(input));
}

// Builds a CBOR item representing an integer, handling both positive and
// negative values.
cbor_item_t* cbor_build_int(int input);
// Builds a CBOR item representing an unsigned integer, selecting the
// appropriate width based on the input size.
cbor_item_t* cbor_build_uint(uint input);
// Builds a CBOR item representing a negative integer, selecting the appropriate
// width based on the input size.
cbor_item_t* cbor_build_negint(uint input);
//  Builds a CBOR item representing a float by encoding the input as half,
//  single, or
// double precision based on the closest matching precision.
absl::StatusOr<cbor_item_t*> cbor_build_float(double input);
// Serializes a key-value pair (with a string key and byte string value) into a
// CBOR map and handles errors using the provided error handler.
absl::Status CborSerializeByteString(absl::string_view key,
                                     const std::string& value,
                                     ErrorHandler error_handler,
                                     cbor_item_t& root);
// Serializes a key-value pair (with a string key and floating-point value) into
// a CBOR map and handles errors using the provided error handler.
absl::Status CborSerializeFloat(absl::string_view key, double value,
                                ErrorHandler error_handler, cbor_item_t& root);
// Serializes a key-value pair (with a string key and boolean value) into a CBOR
// map and handles errors using the provided error handler.
absl::Status CborSerializeBool(absl::string_view key, bool value,
                               ErrorHandler error_handler, cbor_item_t& root);
// Serializes a key-value pair (with a string key and integer value) into a CBOR
// map and
//  handles errors using the provided error handler.
absl::Status CborSerializeInt(absl::string_view key, int value,
                              ErrorHandler error_handler, cbor_item_t& root);
// Serializes a key-value pair (with a string key and string value) into a CBOR
// map and handles errors using the provided error handler.
absl::Status CborSerializeString(absl::string_view key, absl::string_view value,
                                 ErrorHandler error_handler, cbor_item_t& root);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CBOR_COMMON_UTIL_H_
