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
  size_t chaff_request_size = 0;
  CompressionType compression_type = CompressionType::kUncompressed;
};

struct ResponseMetadata {
  // The size of the *compressed and encrypted* request.
  size_t request_size = 0;

  size_t response_size = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_GRPC_REQUEST_CONFIG_
