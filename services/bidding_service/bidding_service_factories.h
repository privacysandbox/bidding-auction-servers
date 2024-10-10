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

#ifndef SERVICES_BIDDING_SERVICE_BIDDING_SERVICE_FACTORIES_H_
#define SERVICES_BIDDING_SERVICE_BIDDING_SERVICE_FACTORIES_H_

#include <memory>
#include <utility>

#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/bidding_service.h"
#include "services/bidding_service/byob/generate_bid_byob_dispatch_client.h"
#include "services/bidding_service/generate_bids_binary_reactor.h"
#include "services/bidding_service/generate_bids_reactor.h"
#include "services/bidding_service/protected_app_signals_generate_bids_binary_reactor.h"
#include "services/bidding_service/protected_app_signals_generate_bids_reactor.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"

namespace privacy_sandbox::bidding_auction_servers {

inline GenerateBidsReactorFactory GetProtectedAudienceByobReactorFactory(
    GenerateBidByobDispatchClient& byob_client,
    server_common::Executor* executor) {
  return [&byob_client, executor](
             grpc::CallbackServerContext* context,
             const GenerateBidsRequest* request, GenerateBidsResponse* response,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client,
             const BiddingServiceRuntimeConfig& runtime_config) {
    CHECK(runtime_config.is_protected_audience_enabled);
    auto generate_bids_binary_reactor =
        std::make_unique<GenerateBidsBinaryReactor>(
            context, byob_client, request, response, key_fetcher_manager,
            crypto_client, executor, runtime_config);
    return generate_bids_binary_reactor.release();
  };
}

inline ProtectedAppSignalsGenerateBidsReactorFactory
GetProtectedAppSignalsByobReactorFactory() {
  return [](grpc::CallbackServerContext* context,
            const GenerateProtectedAppSignalsBidsRequest* request,
            const BiddingServiceRuntimeConfig& runtime_config,
            GenerateProtectedAppSignalsBidsResponse* response,
            server_common::KeyFetcherManagerInterface* key_fetcher_manager,
            CryptoClientWrapperInterface* crypto_client,
            KVAsyncClient* ad_retrieval_client, KVAsyncClient* kv_async_client,
            EgressSchemaCache* egress_schema_cache,
            EgressSchemaCache* limited_egress_schema_cache) {
    // PAS is currently not supported with BYOB. This is a barebones reactor
    // that returns Unimplemented error when execute is called.
    auto generate_bids_binary_reactor =
        std::make_unique<ProtectedAppSignalsGenerateBidsBinaryReactor>(
            runtime_config, request, response, key_fetcher_manager,
            crypto_client);
    return generate_bids_binary_reactor.release();
  };
}

inline GenerateBidsReactorFactory GetProtectedAudienceV8ReactorFactory(
    V8DispatchClient& v8_client, bool enable_bidding_service_benchmark) {
  return [&v8_client, &enable_bidding_service_benchmark](
             grpc::CallbackServerContext* context,
             const GenerateBidsRequest* request, GenerateBidsResponse* response,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client,
             const BiddingServiceRuntimeConfig& runtime_config) {
    DCHECK(runtime_config.is_protected_audience_enabled);
    std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger;
    if (enable_bidding_service_benchmark) {
      benchmarkingLogger =
          std::make_unique<BiddingBenchmarkingLogger>(FormatTime(absl::Now()));
    } else {
      benchmarkingLogger = std::make_unique<BiddingNoOpLogger>();
    }
    auto generate_bids_reactor = std::make_unique<GenerateBidsReactor>(
        context, v8_client, request, response, std::move(benchmarkingLogger),
        key_fetcher_manager, crypto_client, runtime_config);
    return generate_bids_reactor.release();
  };
}

inline ProtectedAppSignalsGenerateBidsReactorFactory
GetProtectedAppSignalsV8ReactorFactory(V8DispatchClient& v8_client) {
  return [&v8_client](
             grpc::CallbackServerContext* context,
             const GenerateProtectedAppSignalsBidsRequest* request,
             const BiddingServiceRuntimeConfig& runtime_config,
             GenerateProtectedAppSignalsBidsResponse* response,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client,
             KVAsyncClient* ad_retrieval_client, KVAsyncClient* kv_async_client,
             EgressSchemaCache* egress_schema_cache,
             EgressSchemaCache* limited_egress_schema_cache) {
    DCHECK(runtime_config.is_protected_app_signals_enabled);
    auto generate_bids_reactor =
        std::make_unique<ProtectedAppSignalsGenerateBidsReactor>(
            context, v8_client, runtime_config, request, response,
            key_fetcher_manager, crypto_client, ad_retrieval_client,
            kv_async_client, egress_schema_cache, limited_egress_schema_cache);
    return generate_bids_reactor.release();
  };
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BIDDING_SERVICE_FACTORIES_H_
