// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/util/client_contexts.h"

namespace privacy_sandbox::bidding_auction_servers {

grpc::ClientContext* ClientContexts::Add() {
  auto context = std::make_unique<grpc::ClientContext>();
  auto* context_ptr = context.get();
  client_contexts_.emplace_back(std::move(context));
  return context_ptr;
}

grpc::ClientContext* ClientContexts::Add(
    const absl::flat_hash_map<std::string, std::string>& request_metadata) {
  grpc::ClientContext* context = Add();
  for (const auto& it : request_metadata) {
    context->AddMetadata(it.first, it.second);
  }
  return context;
}

void ClientContexts::CancelAll() {
  for (const auto& context : client_contexts_) {
    context->TryCancel();
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
