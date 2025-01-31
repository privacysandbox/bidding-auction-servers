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
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/metric/server_definition.h"
#include "src/logger/request_context_impl.h"

namespace privacy_sandbox::bidding_auction_servers {

// RomaRequestContext holds B&A server level request context, which can be used
// for logging and metrics in roma callbacks.
class RomaRequestContext {
 public:
  RomaRequestContext(
      const absl::btree_map<std::string, std::string>& context_map,
      const privacy_sandbox::server_common::ConsentedDebugConfiguration&
          debug_config,
      absl::AnyInvocable<privacy_sandbox::server_common::DebugInfo*()>
          debug_info)
      : request_logging_context_(context_map, debug_config,
                                 std::move(debug_info)) {}

  RequestLogContext& GetLogContext() { return request_logging_context_; }

  // Set the unique bidding metric context
  void SetMetricContext(std::variant<std::unique_ptr<metric::BiddingContext>,
                                     std::unique_ptr<metric::AuctionContext>>
                            context) {
    metric_context_ = std::move(context);
  }

  // Get the shared bidding metric context
  absl::StatusOr<std::variant<metric::BiddingContext*, metric::AuctionContext*>>
  GetMetricContext() const {
    return std::visit(
        [](auto& context)
            -> absl::StatusOr<std::variant<metric::BiddingContext*,
                                           metric::AuctionContext*>> {
          if (context == nullptr) {
            return absl::NotFoundError("Metric context not initialized.");
          } else {
            return context.get();
          }
        },
        metric_context_);
  }

  bool IsConsented() { return request_logging_context_.is_consented(); }

 private:
  RequestLogContext request_logging_context_;
  std::variant<std::unique_ptr<metric::BiddingContext>,
               std::unique_ptr<metric::AuctionContext>>
      metric_context_;
};

class RomaRequestContextFactory;

// Shared RomaRequestContext that can be used as a part of the Roma worker
// dispatch request metadata.
class RomaRequestSharedContext {
 public:
  RomaRequestSharedContext() {}

  // The returned status indicates if RomaRequestContext has gone out of the
  // scope. This can happen during Roma request processing timeout during which
  // the caller owning the context could have returned.
  absl::StatusOr<std::shared_ptr<RomaRequestContext>> GetRomaRequestContext()
      const {
    std::shared_ptr<RomaRequestContext> shared_context =
        roma_request_context_.lock();
    if (!shared_context) {
      return absl::UnavailableError("RomaRequestContext is not available");
    }

    return shared_context;
  }

  absl::StatusOr<std::variant<metric::BiddingContext*, metric::AuctionContext*>>
  GetMetricContext() const {
    PS_ASSIGN_OR_RETURN(std::shared_ptr<RomaRequestContext> shared_context,
                        GetRomaRequestContext());
    return shared_context->GetMetricContext();
  }

  friend class RomaRequestContextFactory;

 private:
  explicit RomaRequestSharedContext(
      const std::shared_ptr<RomaRequestContext>& roma_request_context)
      : roma_request_context_(roma_request_context) {}
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
          debug_config,
      absl::AnyInvocable<privacy_sandbox::server_common::DebugInfo*()>
          debug_info)
      : roma_request_context_(std::make_shared<RomaRequestContext>(
            context_map, debug_config, std::move(debug_info))) {}

  RomaRequestSharedContext Create() {
    return RomaRequestSharedContext(roma_request_context_);
  }

  RomaRequestContextFactory(RomaRequestContextFactory&& other) = delete;
  RomaRequestContextFactory& operator=(RomaRequestContextFactory&& other) =
      delete;
  RomaRequestContextFactory(const RomaRequestContextFactory&) = delete;
  RomaRequestContextFactory& operator=(const RomaRequestContextFactory&) =
      delete;

 private:
  std::shared_ptr<RomaRequestContext> roma_request_context_;
};

template <typename RequestT>
RomaRequestSharedContext RomaSharedContextWithMetric(
    const RequestT* request, RomaRequestSharedContext shared_context,
    RequestLogContext& log_context) {
  // Create new metric context for custom metrics.
  metric::MetricContextMap<RequestT>()->Get(request);
  auto metric_context = metric::MetricContextMap<RequestT>()->Remove(request);
  CHECK_OK(metric_context);
  absl::StatusOr<std::shared_ptr<RomaRequestContext>> roma_shared_context =
      shared_context.GetRomaRequestContext();
  if (roma_shared_context.ok()) {
    (*roma_shared_context)->SetMetricContext(std::move(*metric_context));
  } else {
    PS_LOG(ERROR, log_context) << "Failed to retrieve RomaRequestContext: "
                               << roma_shared_context.status();
  }
  return shared_context;
}
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_REQUEST_CONTEXT_H_
