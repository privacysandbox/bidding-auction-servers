//  Copyright 2025 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/common/util/request_metadata.h"

#include <utility>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

class GrpcMetadataValidator {
 public:
  GrpcMetadataValidator() {
    // Supported gRPC header key characters are defined here:
    // https://github.com/grpc/grpc/blob/ca0d85cea5242d6740ba6cf1dc5c0b9647856fca/src/core/lib/surface/validate_metadata.cc#L33-L42
    for (int i = 'a'; i <= 'z'; i++) {
      valid_header_bits.set(i);
    }

    for (int i = '0'; i <= '9'; i++) {
      valid_header_bits.set(i);
    }

    valid_header_bits.set('-');
    valid_header_bits.set('_');
    valid_header_bits.set('.');

    // Supported gRPC header key characters are defined here:
    // https://github.com/grpc/grpc/blob/ca0d85cea5242d6740ba6cf1dc5c0b9647856fca/src/core/lib/surface/validate_metadata.cc#L107-L114
    for (int i = 32; i <= 126; i++) {
      valid_header_value_bits.set(i);
    }
  }

  bool IsValidHeaderKey(absl::string_view candidate_key) {
    for (uint8_t c : candidate_key) {
      if (!valid_header_bits[c]) {
        return false;
      }
    }
    return true;
  }

  bool IsValidHeaderValue(absl::string_view candidate_value) {
    for (uint8_t c : candidate_value) {
      if (!valid_header_value_bits[c]) {
        return false;
      }
    }
    return true;
  }

 private:
  std::bitset<256> valid_header_bits;
  std::bitset<256> valid_header_value_bits;
};

GrpcMetadataValidator GetGrpcMetadataValidator() {
  static const absl::NoDestructor<GrpcMetadataValidator> validator;
  return *validator;
}

}  // namespace

bool IsValidHeaderKey(absl::string_view candidate_key) {
  return GetGrpcMetadataValidator().IsValidHeaderKey(candidate_key);
}

bool IsValidHeaderValue(absl::string_view candidate_value) {
  return GetGrpcMetadataValidator().IsValidHeaderValue(candidate_value);
}

bool IsValidHeader(absl::string_view candidate_key,
                   absl::string_view candidate_value) {
  if (!IsValidHeaderKey(candidate_key) ||
      !IsValidHeaderValue(candidate_value)) {
    PS_VLOG(kNoisyWarn) << absl::StrFormat(kIllegalHeaderError, candidate_key,
                                           candidate_value);
    return false;
  }

  return true;
}

}  // namespace privacy_sandbox::bidding_auction_servers
