//  Copyright 2022 Google LLC
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

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "sandbox/sandbox_executor.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/bidding_code_fetch_config.pb.h"
#include "services/bidding_service/bidding_service.h"
#include "services/bidding_service/bidding_service_factories.h"
#include "services/bidding_service/buyer_code_fetch_manager.h"
#include "services/bidding_service/byob/buyer_code_fetch_manager_byob.h"
#include "services/bidding_service/byob/generate_bid_byob_dispatch_client.h"
#include "services/bidding_service/cddl_spec_cache.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/constants.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/bidding_service/egress_features/adtech_schema_fetcher.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/bidding_service/egress_schema_fetch_config.pb.h"
#include "services/bidding_service/inference/inference_utils.h"
#include "services/bidding_service/inference/model_fetcher_metric.h"
#include "services/bidding_service/inference/periodic_model_fetcher.h"
#include "services/bidding_service/protected_app_signals_generate_bids_reactor.h"
#include "services/bidding_service/runtime_flags.h"
#include "services/common/blob_fetch/blob_fetcher.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/data_fetch/periodic_code_fetcher.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/feature_flags.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "services/common/util/file_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/tcmalloc_utils.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/interface/cpio.h"
#include "src/public/cpio/proto/blob_storage_service/v1/blob_storage_service.pb.h"
#include "src/util/rlimit_core_config.h"
#include "src/util/status_macro/status_macros.h"

ABSL_FLAG(std::optional<uint16_t>, port, std::nullopt,
          "Port the server is listening on.");
ABSL_FLAG(std::optional<uint16_t>, healthcheck_port, std::nullopt,
          "Non-TLS port dedicated to healthchecks. Must differ from --port.");
ABSL_FLAG(std::optional<bool>, enable_bidding_service_benchmark, std::nullopt,
          "Benchmark the bidding service.");
ABSL_FLAG(
    bool, init_config_client, false,
    "Initialize config client to fetch any runtime flags not supplied from"
    " command line from cloud metadata store. False by default.");
ABSL_FLAG(
    std::optional<std::string>, buyer_code_fetch_config, std::nullopt,
    "The JSON string for config fields necessary for AdTech code fetching.");
ABSL_FLAG(std::optional<std::string>, egress_schema_fetch_config, std::nullopt,
          "The JSON string for config fields necessary for AdTech egress "
          "schema fetching.");
ABSL_FLAG(
    std::optional<std::int64_t>, udf_num_workers, std::nullopt,
    "The number of workers/threads for executing AdTech code in parallel.");
ABSL_FLAG(std::optional<std::int64_t>, js_worker_queue_len, std::nullopt,
          "The length of queue size for a single JS execution worker.");
ABSL_FLAG(std::optional<std::string>, tee_ad_retrieval_kv_server_addr, "",
          "Ad Retrieval KV Server Address");
ABSL_FLAG(std::optional<std::string>,
          tee_ad_retrieval_kv_server_grpc_arg_default_authority, "",
          "Domain for Ad Retrieval KV Server, or other domain for Authority");
ABSL_FLAG(std::optional<std::string>, tee_kv_server_addr, "",
          "KV Server Address to use for ads metadata lookup");
ABSL_FLAG(std::optional<std::string>, tee_kv_server_grpc_arg_default_authority,
          "", "Domain for KV Server, or other domain for Authority");
ABSL_FLAG(std::optional<int>, ad_retrieval_timeout_ms, std::nullopt,
          "The time in milliseconds to wait for the ads retrieval to complete");
ABSL_FLAG(std::optional<bool>, ad_retrieval_kv_server_egress_tls, std::nullopt,
          "If true, ad retrieval service gRPC client uses TLS.");
ABSL_FLAG(std::optional<bool>, kv_server_egress_tls, std::nullopt,
          "If true, KV service gRPC client uses TLS.");
ABSL_FLAG(std::optional<int64_t>,
          bidding_tcmalloc_background_release_rate_bytes_per_second,
          std::nullopt,
          "Amount of cached memory in bytes that is returned back to the "
          "system per second");
ABSL_FLAG(std::optional<int64_t>, bidding_tcmalloc_max_total_thread_cache_bytes,
          std::nullopt,
          "Maximum amount of cached memory in bytes across all threads (or "
          "logical CPUs)");

namespace privacy_sandbox::bidding_auction_servers {

using bidding_service::BuyerCodeFetchConfig;
using ::google::scp::cpio::BlobStorageClientFactory;
using ::google::scp::cpio::BlobStorageClientInterface;
using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::grpc::Server;
using ::grpc::ServerBuilder;

// Collection of objects required for egress to work
struct EgressInfo {
  // A cache of adtech provided schemas. This is updated once the adtech
  // schema fetcher fetches a schema from adtech provided URL. This is later
  // used to serialize the adtech provided features.
  std::unique_ptr<EgressSchemaCache> egress_schema_cache;
  // An HTTP fetcher object that is used to fetch egress schema from adtech
  // provided URL.
  std::unique_ptr<AdtechSchemaFetcher> adtech_schema_fetcher;
};

absl::StatusOr<EgressInfo> StartEgressSchemaFetch(
    const TrustedServersConfigClient& config_client,
    const std::string& egress_schema_url,
    const bidding_service::EgressSchemaFetchConfig& egress_schema_fetch_config,
    server_common::Executor* executor,
    MultiCurlHttpFetcherAsync* http_fetcher_async) {
  auto cddl_spec_cache = std::make_unique<CddlSpecCache>(
      "services/bidding_service/egress_cddl_spec/");
  cddl_spec_cache->Init();
  auto egress_schema_cache =
      std::make_unique<EgressSchemaCache>(std::move(cddl_spec_cache));
  PS_LOG(INFO) << "Loading the adtech schema from: " << egress_schema_url;
  auto adtech_schema_fetcher = std::make_unique<AdtechSchemaFetcher>(
      std::vector<std::string>{egress_schema_url},
      absl::Milliseconds(egress_schema_fetch_config.url_fetch_period_ms()),
      absl::Milliseconds(egress_schema_fetch_config.url_fetch_timeout_ms()),
      http_fetcher_async, executor, egress_schema_cache.get());
  PS_RETURN_IF_ERROR(adtech_schema_fetcher->Start())
      << "Unable to start fetching the adtech egress schema";
  return EgressInfo{.egress_schema_cache = std::move(egress_schema_cache),
                    .adtech_schema_fetcher = std::move(adtech_schema_fetcher)};
}

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    absl::string_view config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_healthcheck_port, HEALTHCHECK_PORT);
  config_client.SetFlag(FLAGS_enable_bidding_service_benchmark,
                        ENABLE_BIDDING_SERVICE_BENCHMARK);
  config_client.SetFlag(FLAGS_test_mode, TEST_MODE);
  config_client.SetFlag(FLAGS_roma_timeout_ms, ROMA_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_public_key_endpoint, PUBLIC_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_primary_coordinator_private_key_endpoint,
                        PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_secondary_coordinator_private_key_endpoint,
                        SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_primary_coordinator_account_identity,
                        PRIMARY_COORDINATOR_ACCOUNT_IDENTITY);
  config_client.SetFlag(FLAGS_secondary_coordinator_account_identity,
                        SECONDARY_COORDINATOR_ACCOUNT_IDENTITY);
  config_client.SetFlag(FLAGS_gcp_primary_workload_identity_pool_provider,
                        GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER);
  config_client.SetFlag(FLAGS_gcp_secondary_workload_identity_pool_provider,
                        GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER);
  config_client.SetFlag(FLAGS_gcp_primary_key_service_cloud_function_url,
                        GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL);
  config_client.SetFlag(FLAGS_gcp_secondary_key_service_cloud_function_url,
                        GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL);

  config_client.SetFlag(FLAGS_primary_coordinator_region,
                        PRIMARY_COORDINATOR_REGION);
  config_client.SetFlag(FLAGS_secondary_coordinator_region,
                        SECONDARY_COORDINATOR_REGION);
  config_client.SetFlag(FLAGS_private_key_cache_ttl_seconds,
                        PRIVATE_KEY_CACHE_TTL_SECONDS);
  config_client.SetFlag(FLAGS_key_refresh_flow_run_frequency_seconds,
                        KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS);
  config_client.SetFlag(FLAGS_telemetry_config, TELEMETRY_CONFIG);
  config_client.SetFlag(FLAGS_buyer_code_fetch_config, BUYER_CODE_FETCH_CONFIG);
  config_client.SetFlag(FLAGS_egress_schema_fetch_config,
                        EGRESS_SCHEMA_FETCH_CONFIG);
  config_client.SetFlag(FLAGS_udf_num_workers, UDF_NUM_WORKERS);
  config_client.SetFlag(FLAGS_js_worker_queue_len, JS_WORKER_QUEUE_LEN);
  config_client.SetFlag(FLAGS_consented_debug_token, CONSENTED_DEBUG_TOKEN);
  config_client.SetFlag(FLAGS_enable_otel_based_logging,
                        ENABLE_OTEL_BASED_LOGGING);
  config_client.SetFlag(FLAGS_enable_protected_app_signals,
                        ENABLE_PROTECTED_APP_SIGNALS);
  config_client.SetFlag(FLAGS_enable_protected_audience,
                        ENABLE_PROTECTED_AUDIENCE);
  config_client.SetFlag(FLAGS_tee_ad_retrieval_kv_server_addr,
                        TEE_AD_RETRIEVAL_KV_SERVER_ADDR);
  config_client.SetFlag(
      FLAGS_tee_ad_retrieval_kv_server_grpc_arg_default_authority,
      TEE_AD_RETRIEVAL_KV_SERVER_GRPC_ARG_DEFAULT_AUTHORITY);
  config_client.SetFlag(FLAGS_tee_kv_server_addr, TEE_KV_SERVER_ADDR);
  config_client.SetFlag(FLAGS_tee_kv_server_grpc_arg_default_authority,
                        TEE_KV_SERVER_GRPC_ARG_DEFAULT_AUTHORITY);
  config_client.SetFlag(FLAGS_ad_retrieval_timeout_ms, AD_RETRIEVAL_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_ps_verbosity, PS_VERBOSITY);
  config_client.SetFlag(FLAGS_max_allowed_size_debug_url_bytes,
                        MAX_ALLOWED_SIZE_DEBUG_URL_BYTES);
  config_client.SetFlag(FLAGS_max_allowed_size_all_debug_urls_kb,
                        MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB);
  config_client.SetFlag(FLAGS_ad_retrieval_kv_server_egress_tls,
                        AD_RETRIEVAL_KV_SERVER_EGRESS_TLS);
  config_client.SetFlag(FLAGS_kv_server_egress_tls, KV_SERVER_EGRESS_TLS);
  config_client.SetFlag(FLAGS_inference_sidecar_binary_path,
                        INFERENCE_SIDECAR_BINARY_PATH);
  config_client.SetFlag(FLAGS_inference_model_bucket_name,
                        INFERENCE_MODEL_BUCKET_NAME);
  config_client.SetFlag(FLAGS_inference_model_bucket_paths,
                        INFERENCE_MODEL_BUCKET_PATHS);
  config_client.SetFlag(FLAGS_inference_model_config_path,
                        INFERENCE_MODEL_CONFIG_PATH);
  config_client.SetFlag(FLAGS_inference_sidecar_runtime_config,
                        INFERENCE_SIDECAR_RUNTIME_CONFIG);
  config_client.SetFlag(
      FLAGS_bidding_tcmalloc_background_release_rate_bytes_per_second,
      BIDDING_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND);
  config_client.SetFlag(FLAGS_bidding_tcmalloc_max_total_thread_cache_bytes,
                        BIDDING_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES);

  if (absl::GetFlag(FLAGS_init_config_client)) {
    PS_RETURN_IF_ERROR(config_client.Init(config_param_prefix)).LogError()
        << "Config client failed to initialize.";
  }
  // Set verbosity
  server_common::log::SetGlobalPSVLogLevel(
      config_client.GetIntParameter(PS_VERBOSITY));

  const bool enable_protected_app_signals =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  const bool enable_protected_audience =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_AUDIENCE);
  CHECK(enable_protected_audience || enable_protected_app_signals)
      << "Neither Protected Audience nor Protected App Signals support "
         "enabled.";
  PS_LOG(INFO) << "Protected App Signals support enabled on the service: "
               << enable_protected_app_signals;
  PS_LOG(INFO) << "Protected Audience support enabled on the service: "
               << enable_protected_audience;
  PS_LOG(INFO) << "Successfully constructed the config client.";
  return config_client;
}

absl::string_view GetStringParameterSafe(
    const TrustedServersConfigClient& client, absl::string_view name) {
  if (client.HasParameter(name)) {
    return client.GetStringParameter(name);
  }
  return "";
}

inline bool UdfConfigHasByob(
    const bidding_service::BuyerCodeFetchConfig& udf_config) {
  return (udf_config.fetch_mode() == blob_fetch::FETCH_MODE_LOCAL &&
          !udf_config.bidding_executable_path().empty()) ||
         (udf_config.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET &&
          !udf_config.protected_auction_bidding_executable_bucket().empty() &&
          !udf_config.protected_auction_bidding_executable_bucket_default_blob()
               .empty()) ||
         (udf_config.fetch_mode() == blob_fetch::FETCH_MODE_URL &&
          !udf_config.bidding_executable_url().empty());
}

DispatchConfig GetV8DispatchConfig(
    const TrustedServersConfigClient& config_client, bool enable_inference) {
  return [&config_client, &enable_inference]() {
    DispatchConfig config;
    config.worker_queue_max_items =
        config_client.GetIntParameter(JS_WORKER_QUEUE_LEN);
    config.number_of_workers = config_client.GetIntParameter(UDF_NUM_WORKERS);
    if (enable_inference) {
      PS_LOG(INFO) << "Register runInference API.";
      auto run_inference_function_object =
          std::make_unique<google::scp::roma::FunctionBindingObjectV2<
              RomaRequestSharedContextBidding>>();
      run_inference_function_object->function_name =
          std::string(inference::kInferenceFunctionName);
      run_inference_function_object->function = inference::RunInference;
      config.RegisterFunctionBinding(std::move(run_inference_function_object));
      PS_LOG(INFO) << "RunInference registered.";

      PS_LOG(INFO) << "Register getModelPaths API.";
      auto get_model_paths_function_object =
          std::make_unique<google::scp::roma::FunctionBindingObjectV2<
              RomaRequestSharedContextBidding>>();
      get_model_paths_function_object->function_name =
          std::string(inference::kGetModelPathsFunctionName);
      get_model_paths_function_object->function = inference::GetModelPaths;
      config.RegisterFunctionBinding(
          std::move(get_model_paths_function_object));
      PS_LOG(INFO) << "getModelPaths registered.";

      PS_LOG(INFO) << "Start the inference sidecar.";
      // This usage of the following two flags is not consistent with rest
      // of the codebase, where we use the parameter from the config client
      // directly instead of passing it back to the absl flag.
      absl::SetFlag(
          &FLAGS_inference_sidecar_binary_path,
          GetStringParameterSafe(config_client, INFERENCE_SIDECAR_BINARY_PATH));
      absl::SetFlag(&FLAGS_inference_sidecar_runtime_config,
                    GetStringParameterSafe(config_client,
                                           INFERENCE_SIDECAR_RUNTIME_CONFIG));
      inference::SandboxExecutor& inference_executor = inference::Executor();
      CHECK_EQ(inference_executor.StartSandboxee().code(),
               absl::StatusCode::kOk);
    }
    return config;
  }();
}

// TODO(b/356153749): Deprecate once we support dynamic partitioning metrics.
std::vector<std::string> GetModels(
    std::string_view inference_model_bucket_paths) {
  if (!inference_model_bucket_paths.empty()) {
    std::vector<std::string> models;
    for (absl::string_view path :
         absl::StrSplit(inference_model_bucket_paths, ',')) {
      models.emplace_back(path);
    }
    return models;
  }
  return {};
}

// Brings up the gRPC BiddingService on FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));
  const std::string_view inference_model_bucket_paths =
      GetStringParameterSafe(config_client, INFERENCE_MODEL_BUCKET_PATHS);
  std::vector<std::string> models = GetModels(inference_model_bucket_paths);
  // InitTelemetry right after config_client being initialized
  InitTelemetry<google::protobuf::Message>(
      config_util, config_client, metric::kBs, /* buyer_list */ {}, models);
  PS_LOG(INFO, SystemLogContext()) << "server parameters:\n"
                                   << config_client.DebugString();

  MaySetBackgroundReleaseRate(config_client.GetInt64Parameter(
      BIDDING_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND));
  MaySetMaxTotalThreadCacheBytes(config_client.GetInt64Parameter(
      BIDDING_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES));
  std::string_view port = config_client.GetStringParameter(PORT);
  std::string server_address = absl::StrCat("0.0.0.0:", port);

  // Convert Json string into a BiddingCodeBlobFetcherConfig proto
  CHECK(!config_client.GetStringParameter(BUYER_CODE_FETCH_CONFIG).empty())
      << "BUYER_CODE_FETCH_CONFIG is a mandatory flag.";
  BuyerCodeFetchConfig udf_config;
  absl::Status result = google::protobuf::util::JsonStringToMessage(
      config_client.GetStringParameter(BUYER_CODE_FETCH_CONFIG).data(),
      &udf_config);
  PS_RETURN_IF_ERROR(result)
      << "Could not parse BUYER_CODE_FETCH_CONFIG JsonString to "
         "a proto message: "
      << result.message();

  server_common::GrpcInit gprc_init;
  std::unique_ptr<server_common::Executor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());
  auto http_fetcher_async =
      std::make_unique<MultiCurlHttpFetcherAsync>(executor.get());

  std::string_view inference_sidecar_binary_path =
      GetStringParameterSafe(config_client, INFERENCE_SIDECAR_BINARY_PATH);
  const bool enable_inference = !inference_sidecar_binary_path.empty();
  const bool enable_protected_audience =
      config_client.HasParameter(ENABLE_PROTECTED_AUDIENCE) &&
      config_client.GetBooleanParameter(ENABLE_PROTECTED_AUDIENCE);
  const bool enable_protected_app_signals =
      config_client.HasParameter(ENABLE_PROTECTED_APP_SIGNALS) &&
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  const bool enable_bidding_service_benchmark =
      config_client.HasParameter(ENABLE_BIDDING_SERVICE_BENCHMARK) &&
      config_client.GetBooleanParameter(ENABLE_BIDDING_SERVICE_BENCHMARK);
  const bool enable_buyer_debug_url_generation =
      udf_config.enable_buyer_debug_url_generation();
  const bool enable_private_aggregate_reporting =
      udf_config.enable_private_aggregate_reporting();

  std::unique_ptr<GenerateBidByobDispatchClient> byob_client;
  std::unique_ptr<V8Dispatcher> v8_dispatcher;
  std::unique_ptr<V8DispatchClient> v8_client;
  std::unique_ptr<BuyerCodeFetchManager> udf_fetcher;

  GenerateBidsReactorFactory generate_bids_reactor_factory;
  ProtectedAppSignalsGenerateBidsReactorFactory
      protected_app_signals_generate_bids_reactor_factory;

  if (UdfConfigHasByob(udf_config)) {
    CHECK(enable_protected_audience) << kProtectedAudienceMustBeEnabled;
    CHECK(!enable_protected_app_signals) << kProtectedAppSignalsMustBeDisabled;

    PS_ASSIGN_OR_RETURN(auto temp_client,
                        GenerateBidByobDispatchClient::Create(
                            config_client.GetIntParameter(UDF_NUM_WORKERS)));
    byob_client =
        std::make_unique<GenerateBidByobDispatchClient>(std::move(temp_client));

    udf_fetcher = std::make_unique<BuyerCodeFetchManagerByob>(
        executor.get(), http_fetcher_async.get(), byob_client.get(),
        BlobStorageClientFactory::Create(), udf_config);
    PS_RETURN_IF_ERROR(udf_fetcher->Init())
        << "Failed to initialize UDF fetch.";

    generate_bids_reactor_factory =
        GetProtectedAudienceByobReactorFactory(*byob_client, executor.get());
    protected_app_signals_generate_bids_reactor_factory =
        GetProtectedAppSignalsByobReactorFactory();
  } else {
    v8_dispatcher = std::make_unique<V8Dispatcher>(
        GetV8DispatchConfig(config_client, enable_inference));
    v8_client = std::make_unique<V8DispatchClient>(*v8_dispatcher.get());
    PS_RETURN_IF_ERROR(v8_dispatcher->Init())
        << "Could not start V8 dispatcher.";

    udf_fetcher = std::make_unique<BuyerCodeFetchManager>(
        executor.get(), http_fetcher_async.get(), v8_dispatcher.get(),
        BlobStorageClientFactory::Create(), udf_config,
        enable_protected_audience, enable_protected_app_signals);
    PS_RETURN_IF_ERROR(udf_fetcher->Init())
        << "Failed to initialize UDF fetch.";

    generate_bids_reactor_factory = GetProtectedAudienceV8ReactorFactory(
        *v8_client, enable_bidding_service_benchmark);
    protected_app_signals_generate_bids_reactor_factory =
        GetProtectedAppSignalsV8ReactorFactory(*v8_client);
  }

  bool init_config_client = absl::GetFlag(FLAGS_init_config_client);
  std::unique_ptr<inference::PeriodicModelFetcher> model_fetcher;
  if (enable_inference) {
    if (init_config_client) {
      std::string_view bucket_name =
          GetStringParameterSafe(config_client, INFERENCE_MODEL_BUCKET_NAME);
      std::string_view model_config_path =
          GetStringParameterSafe(config_client, INFERENCE_MODEL_CONFIG_PATH);

      // TODO(b/356153749): Deprecate static model fetcher.
      if (!bucket_name.empty() && !inference_model_bucket_paths.empty()) {
        std::unique_ptr<BlobStorageClientInterface> blob_storage_client =
            BlobStorageClientFactory::Create();
        auto blob_fetcher = std::make_unique<BlobFetcher>(
            bucket_name, executor.get(), std::move(blob_storage_client));
        CHECK(blob_fetcher->FetchSync().ok()) << "FetchSync() failed.";
        const std::vector<BlobFetcher::Blob>& files = blob_fetcher->snapshot();
        PS_LOG(INFO) << "Register models from bucket.";
        if (absl::Status status =
                inference::RegisterModelsFromBucket(bucket_name, models, files);
            !status.ok()) {
          PS_LOG(INFO) << "Skip registering models from bucket: "
                       << status.message();
        }
      } else if (!bucket_name.empty() && !model_config_path.empty()) {
        PS_RETURN_IF_ERROR(inference::AddModelFetcherMetricToBidding())
            << "Failed to initialize model fetching metrics";
        model_fetcher = std::make_unique<inference::PeriodicModelFetcher>(
            model_config_path,
            std::make_unique<BlobFetcher>(bucket_name, executor.get(),
                                          BlobStorageClientFactory::Create()),
            inference::CreateInferenceStub(), executor.get(),
            absl::Milliseconds(config_client.GetInt64Parameter(
                INFERENCE_MODEL_FETCH_PERIOD_MS)));
        PS_RETURN_IF_ERROR(model_fetcher->Start())
            << "Failed to start periodic model fetcher.";
      } else {
        PS_LOG(INFO)
            << "Skip blob fetcher read from a cloud bucket due to empty "
               "required arguements.";
      }
    }

    if (std::optional<std::string> local_paths =
            absl::GetFlag(FLAGS_inference_model_local_paths);
        local_paths.has_value()) {
      PS_LOG(INFO) << "Register models from local for testing.";
      if (absl::Status status = inference::RegisterModelsFromLocal(
              absl::StrSplit(local_paths.value(), ','));
          !status.ok()) {
        PS_LOG(INFO) << "Skip registering models from local: "
                     << status.message();
      }
    }
  }

  std::string tee_ad_retrieval_kv_server_addr = std::string(
      config_client.GetStringParameter(TEE_AD_RETRIEVAL_KV_SERVER_ADDR));
  std::string tee_ad_retrieval_kv_server_grpc_arg_default_authority =
      std::string(config_client.GetStringParameter(
          TEE_AD_RETRIEVAL_KV_SERVER_GRPC_ARG_DEFAULT_AUTHORITY));
  std::string tee_kv_server_addr =
      std::string(config_client.GetStringParameter(TEE_KV_SERVER_ADDR));
  std::string tee_kv_server_grpc_arg_default_authority =
      std::string(config_client.GetStringParameter(
          TEE_KV_SERVER_GRPC_ARG_DEFAULT_AUTHORITY));
  if (enable_protected_app_signals && tee_ad_retrieval_kv_server_addr.empty() &&
      tee_kv_server_addr.empty()) {
    return absl::InvalidArgumentError(
        "Missing: Ad Retrieval server address and KV server address. Must "
        "specify at least one.");
  }

  BiddingServiceRuntimeConfig runtime_config = {
      .tee_ad_retrieval_kv_server_addr =
          std::move(tee_ad_retrieval_kv_server_addr),
      .tee_ad_retrieval_kv_server_grpc_arg_default_authority =
          std::move(tee_ad_retrieval_kv_server_grpc_arg_default_authority),
      .tee_kv_server_addr = std::move(tee_kv_server_addr),
      .tee_kv_server_grpc_arg_default_authority =
          std::move(tee_kv_server_grpc_arg_default_authority),
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .roma_timeout_ms =
          config_client.GetStringParameter(ROMA_TIMEOUT_MS).data(),
      .is_protected_app_signals_enabled = enable_protected_app_signals,
      .is_protected_audience_enabled = enable_protected_audience,
      .ad_retrieval_timeout_ms =
          config_client.GetIntParameter(AD_RETRIEVAL_TIMEOUT_MS),
      .max_allowed_size_debug_url_bytes =
          config_client.GetIntParameter(MAX_ALLOWED_SIZE_DEBUG_URL_BYTES),
      .max_allowed_size_all_debug_urls_kb =
          config_client.GetIntParameter(MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB),
      .ad_retrieval_kv_server_egress_tls =
          config_client.GetBooleanParameter(AD_RETRIEVAL_KV_SERVER_EGRESS_TLS),
      .kv_server_egress_tls =
          config_client.GetBooleanParameter(KV_SERVER_EGRESS_TLS),
      .enable_private_aggregate_reporting = enable_private_aggregate_reporting};

  if (udf_config.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET) {
    if (enable_protected_audience) {
      runtime_config.default_protected_auction_generate_bid_version =
          udf_config.protected_auction_bidding_js_bucket_default_blob();
    }
    if (enable_protected_app_signals) {
      runtime_config.default_protected_app_signals_generate_bid_version =
          udf_config.protected_app_signals_bidding_js_bucket_default_blob();
      runtime_config.default_ad_retrieval_version =
          udf_config.ads_retrieval_bucket_default_blob();
    }
  }

  PS_VLOG(5) << "Fetching egress schema fetch config";
  bidding_service::EgressSchemaFetchConfig egress_schema_fetch_config;
  PS_RETURN_IF_ERROR(google::protobuf::util::JsonStringToMessage(
      config_client.GetStringParameter(EGRESS_SCHEMA_FETCH_CONFIG).data(),
      &egress_schema_fetch_config))
      << "Unable to convert EGRESS_SCHEMA_FETCH_CONFIG from JSON to proto"
         " (Invalid JSON provided?)";
  PS_VLOG(5) << "Fetched egress schema fetch config: "
             << egress_schema_fetch_config.DebugString();
  const bool enable_temporary_unlimited_egress =
      absl::GetFlag(FLAGS_enable_temporary_unlimited_egress);
  absl::StatusOr<EgressInfo> egress_info = absl::UnimplementedError("");
  if (enable_temporary_unlimited_egress && enable_protected_app_signals) {
    PS_LOG(INFO) << "Temporary egress feature is enabled in the binary";
    if (egress_info = StartEgressSchemaFetch(
            config_client,
            egress_schema_fetch_config.temporary_unlimited_egress_schema_url(),
            egress_schema_fetch_config, executor.get(),
            http_fetcher_async.get());
        !egress_info.ok()) {
      PS_LOG(WARNING) << "Unable to load temporary unlimited egress schema: "
                      << egress_info.status();
    }
  } else {
    PS_LOG(INFO) << "Temporary egress feature is not enabled in the binary";
  }
  const int limited_egress_bits = absl::GetFlag(FLAGS_limited_egress_bits);
  PS_LOG(INFO) << "Allowed limited egress bits: " << limited_egress_bits;
  absl::StatusOr<EgressInfo> limited_egress_info = absl::UnimplementedError("");
  const bool egress_enabled = limited_egress_bits > 0;
  if (egress_enabled) {
    PS_LOG(INFO) << "Limited egress feature is enabled in the binary";
    if (limited_egress_info = StartEgressSchemaFetch(
            config_client, egress_schema_fetch_config.egress_schema_url(),
            egress_schema_fetch_config, executor.get(),
            http_fetcher_async.get());
        !limited_egress_info.ok()) {
      PS_LOG(WARNING) << "Unable to load limited egress schema: "
                      << limited_egress_info.status();
    }
  } else {
    PS_LOG(INFO) << "Limited egress feature is not enabled in the binary";
  }

  PS_VLOG(5) << "Creating bidding service instance";
  BiddingService bidding_service(
      std::move(generate_bids_reactor_factory),
      CreateKeyFetcherManager(config_client,
                              enable_protected_app_signals
                                  ? CreatePublicKeyFetcher(config_client)
                                  : nullptr),
      CreateCryptoClient(), std::move(runtime_config),
      std::move(protected_app_signals_generate_bids_reactor_factory),
      /*ad_retrieval_async_client=*/nullptr, /*kv_async_client=*/nullptr,
      enable_temporary_unlimited_egress && egress_info.ok()
          ? std::move(egress_info->egress_schema_cache)
          : nullptr,
      egress_enabled && limited_egress_info.ok()
          ? std::move(limited_egress_info->egress_schema_cache)
          : nullptr);

  PS_VLOG(5) << "Done creating bidding service instance";
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  // This server is expected to accept insecure connections as it will be
  // deployed behind an HTTPS load balancer that terminates TLS.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

  if (config_client.HasParameter(HEALTHCHECK_PORT) &&
      !config_client.GetStringParameter(HEALTHCHECK_PORT).empty()) {
    CHECK(config_client.GetStringParameter(HEALTHCHECK_PORT) !=
          config_client.GetStringParameter(PORT))
        << "Healthcheck port must be unique.";
    builder.AddListeningPort(
        absl::StrCat("0.0.0.0:",
                     config_client.GetStringParameter(HEALTHCHECK_PORT)),
        grpc::InsecureServerCredentials());
  }

  // Set max message size to 256 MB.
  builder.AddChannelArgument(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH,
                             256L * 1024L * 1024L);
  builder.RegisterService(&bidding_service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    return absl::UnavailableError("Error starting Server.");
  }
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  PS_LOG(INFO, SystemLogContext()) << "Server listening on " << server_address;
  server->Wait();
  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers

int main(int argc, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  privacysandbox::server_common::SetRLimits({
      .enable_core_dumps = PS_ENABLE_CORE_DUMPS,
  });
  absl::FailureSignalHandlerOptions options;
  absl::InstallFailureSignalHandler(options);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  google::scp::cpio::CpioOptions cpio_options;

  bool init_config_client = absl::GetFlag(FLAGS_init_config_client);
  if (init_config_client) {
    cpio_options.log_option = google::scp::cpio::LogOption::kConsoleLog;
    CHECK(google::scp::cpio::Cpio::InitCpio(cpio_options).Successful())
        << "Failed to initialize CPIO library";
  }

  CHECK_OK(privacy_sandbox::bidding_auction_servers::RunServer())
      << "Failed to run server.";

  if (init_config_client) {
    google::scp::cpio::Cpio::ShutdownCpio(cpio_options);
  }

  return 0;
}
