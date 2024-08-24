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

#ifndef SERVICES_COMMON_UTIL_CLIENT_CONTEXTS_H_
#define SERVICES_COMMON_UTIL_CLIENT_CONTEXTS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "absl/container/flat_hash_map.h"

namespace privacy_sandbox::bidding_auction_servers {

// This container can be used to keep track of client contexts while sending
// outbound requests from a gRPC reactor. This maintains the ownership for the
// outbound client calls for easier cancellation in case the underlying request
// for that reactor gets cancelled.
//
// Not thread-safe.
class ClientContexts {
 public:
  // Creates a context and owns it.
  // Returns a raw pointer to the created context.
  grpc::ClientContext* Add();

  // Creates a context and owns it. Key/values in request metadata
  // are added to the context.
  // Returns a raw pointer to the created context.
  grpc::ClientContext* Add(
      const absl::flat_hash_map<std::string, std::string>& request_metadata);

  // Tries to cancel all contexts that have been added
  void CancelAll();

 private:
  std::vector<std::unique_ptr<grpc::ClientContext>> client_contexts_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_CLIENT_CONTEXTS_H_
