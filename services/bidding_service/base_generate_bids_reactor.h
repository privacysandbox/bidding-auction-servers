/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_BIDDING_SERVICE_BASE_GENERATE_BIDS_REACTOR_H_
#define SERVICES_BIDDING_SERVICE_BASE_GENERATE_BIDS_REACTOR_H_

#include <optional>
#include <string>

#include "services/bidding_service/data/runtime_config.h"
#include "services/common/code_dispatch/code_dispatch_reactor.h"
#include "services/common/util/consented_debugging_logger.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

template <typename Request, typename RawRequest, typename Response,
          typename RawResponse>
class BaseGenerateBidsReactor
    : public CodeDispatchReactor<Request, RawRequest, Response, RawResponse> {
 public:
  explicit BaseGenerateBidsReactor(
      const CodeDispatchClient& dispatcher, const Request* request,
      Response* response,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const BiddingServiceRuntimeConfig& runtime_config)
      : CodeDispatchReactor<Request, RawRequest, Response, RawResponse>(
            dispatcher, request, response, key_fetcher_manager, crypto_client,
            runtime_config.encryption_enabled),
        enable_buyer_debug_url_generation_(
            runtime_config.enable_buyer_debug_url_generation),
        roma_timeout_ms_(runtime_config.roma_timeout_ms),
        enable_adtech_code_logging_(runtime_config.enable_adtech_code_logging),
        enable_otel_based_logging_(runtime_config.enable_otel_based_logging),
        consented_debug_token_(runtime_config.consented_debug_token) {}

  virtual ~BaseGenerateBidsReactor() = default;

 protected:
  // Gets logging context as key/value pair that is useful for tracking a
  // request through the B&A services.
  ContextLogger::ContextMap GetLoggingContext(
      const RawRequest& generate_bids_request) {
    const auto& logging_context = generate_bids_request.log_context();
    ContextLogger::ContextMap context_map = {
        {kGenerationId, logging_context.generation_id()},
        {kAdtechDebugId, logging_context.adtech_debug_id()}};
    if (generate_bids_request.has_consented_debug_config()) {
      MaybeAddConsentedDebugConfig(
          generate_bids_request.consented_debug_config(), context_map);
    }
    return context_map;
  }

  bool enable_buyer_debug_url_generation_;
  std::string roma_timeout_ms_;
  ContextLogger logger_;
  std::optional<ConsentedDebuggingLogger> debug_logger_;
  bool enable_adtech_code_logging_;
  bool enable_otel_based_logging_;
  std::string consented_debug_token_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BASE_GENERATE_BIDS_REACTOR_H_
