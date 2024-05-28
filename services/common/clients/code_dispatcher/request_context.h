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

#ifndef SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_REQUEST_CONTEXT_H_
#define SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_REQUEST_CONTEXT_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/logger/request_context_impl.h"

namespace privacy_sandbox::bidding_auction_servers {

// RomaRequestContext holds B&A server level request context, which can be used
// for logging and metrics in roma callbacks.
class RomaRequestContext {
 public:
  RomaRequestContext(
      const absl::btree_map<std::string, std::string>& context_map,
      const privacy_sandbox::server_common::ConsentedDebugConfiguration&
          debug_config);

  const privacy_sandbox::server_common::log::ContextImpl& GetLogContext() const;

 private:
  privacy_sandbox::server_common::log::ContextImpl request_logging_context_;
};

class RomaRequestContextFactory;

// Shared RomaRequestContext that can be used as a part of the Roma worker
// dispatch request metadata.
class RomaRequestSharedContext {
 public:
  RomaRequestSharedContext() {}

  absl::StatusOr<std::shared_ptr<RomaRequestContext>> GetRomaRequestContext()
      const;

  friend class RomaRequestContextFactory;

 private:
  explicit RomaRequestSharedContext(
      const std::shared_ptr<RomaRequestContext>& roma_request_context);
  std::weak_ptr<RomaRequestContext> roma_request_context_;
};

// RomaRequestContextFactory holds a RomaRequestContext. Shared copies of this
// context are wrapped by RomaRequestSharedContext and then can be passed to
// Roma workers.
class RomaRequestContextFactory {
 public:
  RomaRequestContextFactory(
      const absl::btree_map<std::string, std::string>& context_map,
      const privacy_sandbox::server_common::ConsentedDebugConfiguration&
          debug_config);
  RomaRequestSharedContext Create();

  RomaRequestContextFactory(RomaRequestContextFactory&& other) = delete;
  RomaRequestContextFactory& operator=(RomaRequestContextFactory&& other) =
      delete;
  RomaRequestContextFactory(const RomaRequestContextFactory&) = delete;
  RomaRequestContextFactory& operator=(const RomaRequestContextFactory&) =
      delete;

 private:
  std::shared_ptr<RomaRequestContext> roma_request_context_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_REQUEST_CONTEXT_H_
