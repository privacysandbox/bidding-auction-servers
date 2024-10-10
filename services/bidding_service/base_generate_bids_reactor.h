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

#include <memory>
#include <optional>
#include <string>

#include "services/bidding_service/data/runtime_config.h"
#include "services/common/clients/code_dispatcher/request_context.h"
#include "services/common/code_dispatch/code_dispatch_reactor.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kDispatchHandlerFunctionNameWithCodeWrapper[] =
    "generateBidEntryFunction";
inline constexpr int kBytesMultiplyer = 1024;

// Returns up the index of the provided enum as int from the underlying enum
// storage.
template <typename T>
inline constexpr int ArgIndex(T arg) {
  return static_cast<std::underlying_type_t<T>>(arg);
}

template <typename Request, typename RawRequest, typename Response,
          typename RawResponse>
class BaseGenerateBidsReactor
    : public CodeDispatchReactor<Request, RawRequest, Response, RawResponse> {
 public:
  explicit BaseGenerateBidsReactor(
      const BiddingServiceRuntimeConfig& runtime_config, const Request* request,
      Response* response,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client)
      : CodeDispatchReactor<Request, RawRequest, Response, RawResponse>(
            request, response, key_fetcher_manager, crypto_client),
        enable_buyer_debug_url_generation_(
            runtime_config.enable_buyer_debug_url_generation),
        roma_timeout_ms_(runtime_config.roma_timeout_ms),
        roma_request_context_factory_(
            GetLoggingContext(this->raw_request_),
            this->raw_request_.consented_debug_config(),
            [this]() { return this->raw_response_.mutable_debug_info(); }),
        log_context_(
            GetLoggingContext(this->raw_request_),
            this->raw_request_.consented_debug_config(),
            [this]() { return this->raw_response_.mutable_debug_info(); }),
        enable_adtech_code_logging_(log_context_.is_consented()),
        max_allowed_size_debug_url_chars_(
            runtime_config.max_allowed_size_debug_url_bytes),
        max_allowed_size_all_debug_urls_chars_(
            kBytesMultiplyer *
            runtime_config.max_allowed_size_all_debug_urls_kb) {}

  virtual ~BaseGenerateBidsReactor() = default;

 protected:
  // Gets logging context as key/value pair that is useful for tracking a
  // request through the B&A services.
  absl::btree_map<std::string, std::string> GetLoggingContext(
      const RawRequest& generate_bids_request) {
    const auto& logging_context = generate_bids_request.log_context();
    return {{kGenerationId, logging_context.generation_id()},
            {kAdtechDebugId, logging_context.adtech_debug_id()}};
  }

  bool enable_buyer_debug_url_generation_;
  std::string roma_timeout_ms_;
  RomaRequestContextFactoryBidding roma_request_context_factory_;
  RequestLogContext log_context_;
  bool enable_adtech_code_logging_;
  int max_allowed_size_debug_url_chars_;
  long max_allowed_size_all_debug_urls_chars_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BASE_GENERATE_BIDS_REACTOR_H_
