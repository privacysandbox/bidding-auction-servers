//  Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_CLIENTS_ASYNC_GRPC_REQUEST_CONFIG_
#define SERVICES_COMMON_CLIENTS_ASYNC_GRPC_REQUEST_CONFIG_

#include <cstddef>

#include "services/common/compression/compression_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

// This can be extended such that each service can have its own specific
// RequestConfig object. That struct type can be added to client_params.h's
// template to avoid casting issues
struct RequestConfig {
  // Whether chaff or not, requests will be padded to a size >=
  // minimum_request_size.
  size_t minimum_request_size = 0;
  CompressionType compression_type = CompressionType::kUncompressed;
  bool is_chaff_request = false;

  bool operator==(const RequestConfig& other) const {
    return minimum_request_size == other.minimum_request_size &&
           compression_type == other.compression_type &&
           is_chaff_request == other.is_chaff_request;
  }
};

// For printing a human readable form of RequestConfig objects, for
// debugging/UTs.
inline std::ostream& operator<<(std::ostream& os, const RequestConfig& config) {
  os << "RequestConfig {"
     << "minimum_request_size: " << config.minimum_request_size << ", "
     << "compression_type: ";
  switch (config.compression_type) {
    case CompressionType::kUncompressed:
      os << "kUncompressed";
      break;
    case CompressionType::kGzip:
      os << "kGzip";
      break;
    default:
      os << "UnknownCompressionType("
         << static_cast<int>(config.compression_type) << ")";
      break;
  }
  os << ", is_chaff_request: " << (config.is_chaff_request ? "true" : "false")
     << "}";
  return os;
}

struct ResponseMetadata {
  // The size of the *compressed and encrypted* request.
  size_t request_size = 0;

  size_t response_size = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_GRPC_REQUEST_CONFIG_
