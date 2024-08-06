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

#include "services/common/clients/code_dispatcher/request_context.h"

namespace privacy_sandbox::bidding_auction_servers {

RomaRequestContext::RomaRequestContext(
    const absl::btree_map<std::string, std::string>& context_map,
    const privacy_sandbox::server_common::ConsentedDebugConfiguration&
        debug_config,
    absl::AnyInvocable<privacy_sandbox::server_common::DebugInfo*()> debug_info)
    : request_logging_context_(context_map, debug_config,
                               std::move(debug_info)) {}

RequestLogContext& RomaRequestContext::GetLogContext() {
  return request_logging_context_;
}

RomaRequestSharedContext::RomaRequestSharedContext(
    const std::shared_ptr<RomaRequestContext>& roma_request_context)
    : roma_request_context_(roma_request_context) {}

absl::StatusOr<std::shared_ptr<RomaRequestContext>>
RomaRequestSharedContext::GetRomaRequestContext() const {
  std::shared_ptr<RomaRequestContext> shared_context =
      roma_request_context_.lock();
  if (!shared_context) {
    return absl::UnavailableError("RomaRequestContext is not available");
  }

  return shared_context;
}

RomaRequestContextFactory::RomaRequestContextFactory(
    const absl::btree_map<std::string, std::string>& context_map,
    const privacy_sandbox::server_common::ConsentedDebugConfiguration&
        debug_config,
    absl::AnyInvocable<privacy_sandbox::server_common::DebugInfo*()> debug_info)
    : roma_request_context_(std::make_shared<RomaRequestContext>(
          context_map, debug_config, std::move(debug_info))) {}

RomaRequestSharedContext RomaRequestContextFactory::Create() {
  return RomaRequestSharedContext(roma_request_context_);
}

}  // namespace privacy_sandbox::bidding_auction_servers
