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
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/bidding_code_fetch_config.pb.h"
#include "services/bidding_service/bidding_service.h"
#include "services/bidding_service/buyer_code_fetch_manager.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/constants.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/bidding_service/inference/inference_utils.h"
#include "services/bidding_service/protected_app_signals_generate_bids_reactor.h"
#include "services/bidding_service/runtime_flags.h"
#include "services/common/blob_fetch/blob_fetcher.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/code_fetch/periodic_code_fetcher.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
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
ABSL_FLAG(
    std::optional<std::int64_t>, js_num_workers, std::nullopt,
    "The number of workers/threads for executing AdTech code in parallel.");
ABSL_FLAG(std::optional<std::int64_t>, js_worker_queue_len, std::nullopt,
          "The length of queue size for a single JS execution worker.");
ABSL_FLAG(std::optional<std::string>, tee_ad_retrieval_kv_server_addr, "",
          "Ad Retrieval KV Server Address");
ABSL_FLAG(std::optional<std::string>, tee_kv_server_addr, "",
          "KV Server Address to use for ads metadata lookup");
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
  config_client.SetFlag(FLAGS_js_num_workers, JS_NUM_WORKERS);
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
  config_client.SetFlag(FLAGS_tee_kv_server_addr, TEE_KV_SERVER_ADDR);
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
  server_common::log::PS_VLOG_IS_ON(
      0, config_client.GetIntParameter(PS_VERBOSITY));

  const bool enable_protected_app_signals =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  const bool enable_protected_audience =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_AUDIENCE);
  if (!enable_protected_audience && !enable_protected_app_signals) {
    ABSL_LOG(WARNING) << "Neither Protected Audience nor Protected App Signals "
                         "support enabled";
  }

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

// Brings up the gRPC BiddingService on FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));

  MaySetBackgroundReleaseRate(config_client.GetInt64Parameter(
      BIDDING_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND));
  MaySetMaxTotalThreadCacheBytes(config_client.GetInt64Parameter(
      BIDDING_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES));
  std::string_view port = config_client.GetStringParameter(PORT);
  std::string server_address = absl::StrCat("0.0.0.0:", port);

  CHECK(!config_client.GetStringParameter(BUYER_CODE_FETCH_CONFIG).empty())
      << "BUYER_CODE_FETCH_CONFIG is a mandatory flag.";

  std::string_view inference_sidecar_binary_path =
      GetStringParameterSafe(config_client, INFERENCE_SIDECAR_BINARY_PATH);
  const bool enable_inference = !inference_sidecar_binary_path.empty();

  auto dispatcher = V8Dispatcher([&config_client, &enable_inference]() {
    DispatchConfig config;
    config.worker_queue_max_items =
        config_client.GetIntParameter(JS_WORKER_QUEUE_LEN);
    config.number_of_workers = config_client.GetIntParameter(JS_NUM_WORKERS);

    if (enable_inference) {
      PS_LOG(INFO) << "Register runInference API.";
      auto run_inference_function_object =
          std::make_unique<google::scp::roma::FunctionBindingObjectV2<
              RomaRequestSharedContext>>();
      run_inference_function_object->function_name =
          std::string(inference::kInferenceFunctionName);
      run_inference_function_object->function = inference::RunInference;
      config.RegisterFunctionBinding(std::move(run_inference_function_object));
      PS_LOG(INFO) << "RunInference registered.";

      PS_LOG(INFO) << "Register getModelPaths API.";
      auto get_model_paths_function_object =
          std::make_unique<google::scp::roma::FunctionBindingObjectV2<
              RomaRequestSharedContext>>();
      get_model_paths_function_object->function_name =
          std::string(inference::kGetModelPathsFunctionName);
      get_model_paths_function_object->function = inference::GetModelPaths;
      config.RegisterFunctionBinding(
          std::move(get_model_paths_function_object));
      PS_LOG(INFO) << "getModelPaths registered.";

      PS_LOG(INFO) << "Start the inference sidecar.";
      // This usage of the following two flags is not consistent with rest of
      // the codebase, where we use the parameter from the config client
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
  }());
  CodeDispatchClient client(dispatcher);
  PS_RETURN_IF_ERROR(dispatcher.Init()) << "Could not start code dispatcher.";

  server_common::GrpcInit gprc_init;
  std::unique_ptr<server_common::Executor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());

  // Convert Json string into a BiddingCodeBlobFetcherConfig proto
  BuyerCodeFetchConfig udf_config;
  absl::Status result = google::protobuf::util::JsonStringToMessage(
      config_client.GetStringParameter(BUYER_CODE_FETCH_CONFIG).data(),
      &udf_config);
  PS_RETURN_IF_ERROR(result)
      << "Could not parse BUYER_CODE_FETCH_CONFIG JsonString to "
         "a proto message: "
      << result.message();

  bool enable_buyer_debug_url_generation =
      udf_config.enable_buyer_debug_url_generation();
  const bool enable_protected_audience =
      config_client.HasParameter(ENABLE_PROTECTED_AUDIENCE) &&
      config_client.GetBooleanParameter(ENABLE_PROTECTED_AUDIENCE);
  const bool enable_protected_app_signals =
      config_client.HasParameter(ENABLE_PROTECTED_APP_SIGNALS) &&
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);

  auto http_fetcher_async =
      std::make_unique<MultiCurlHttpFetcherAsync>(executor.get());
  BuyerCodeFetchManager udf_fetcher(
      executor.get(), http_fetcher_async.get(), &dispatcher,
      BlobStorageClientFactory::Create(), udf_config, enable_protected_audience,
      enable_protected_app_signals);
  PS_RETURN_IF_ERROR(udf_fetcher.Init()) << "Failed to initialize UDF fetch.";

  bool init_config_client = absl::GetFlag(FLAGS_init_config_client);
  if (enable_inference) {
    if (init_config_client) {
      PS_LOG(INFO) << "Start blob fetcher to read from a cloud bucket.";
      std::string_view bucket_name =
          GetStringParameterSafe(config_client, INFERENCE_MODEL_BUCKET_NAME);
      std::string_view bucket_paths =
          GetStringParameterSafe(config_client, INFERENCE_MODEL_BUCKET_PATHS);
      std::vector<std::string> models = absl::StrSplit(bucket_paths, ',');

      if (!bucket_name.empty() && !bucket_paths.empty()) {
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

  bool enable_bidding_service_benchmark =
      config_client.GetBooleanParameter(ENABLE_BIDDING_SERVICE_BENCHMARK);

  InitTelemetry<GenerateBidsRequest>(config_util, config_client, metric::kBs);

  auto generate_bids_reactor_factory =
      [&client, enable_bidding_service_benchmark](
          const GenerateBidsRequest* request, GenerateBidsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const BiddingServiceRuntimeConfig& runtime_config) {
        DCHECK(runtime_config.is_protected_audience_enabled);
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger;
        if (enable_bidding_service_benchmark) {
          benchmarkingLogger = std::make_unique<BiddingBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarkingLogger = std::make_unique<BiddingNoOpLogger>();
        }
        auto generate_bids_reactor = std::make_unique<GenerateBidsReactor>(
            client, request, response, std::move(benchmarkingLogger),
            key_fetcher_manager, crypto_client, runtime_config);
        return generate_bids_reactor.release();
      };

  const bool is_protected_app_signals_enabled =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  std::string tee_ad_retrieval_kv_server_addr = std::string(
      config_client.GetStringParameter(TEE_AD_RETRIEVAL_KV_SERVER_ADDR));
  std::string tee_kv_server_addr =
      std::string(config_client.GetStringParameter(TEE_KV_SERVER_ADDR));
  if (is_protected_app_signals_enabled &&
      tee_ad_retrieval_kv_server_addr.empty() && tee_kv_server_addr.empty()) {
    return absl::InvalidArgumentError(
        "Missing: Ad Retrieval server address "
        "and KV server address. Must specify at "
        "least one.");
  }

  BiddingServiceRuntimeConfig runtime_config = {
      .tee_ad_retrieval_kv_server_addr =
          std::move(tee_ad_retrieval_kv_server_addr),
      .tee_kv_server_addr = std::move(tee_kv_server_addr),
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
          config_client.GetBooleanParameter(KV_SERVER_EGRESS_TLS)};

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

  auto protected_app_signals_generate_bids_reactor_factory =
      [&client](const grpc::CallbackServerContext* context,
                const GenerateProtectedAppSignalsBidsRequest* request,
                const BiddingServiceRuntimeConfig& runtime_config,
                GenerateProtectedAppSignalsBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                KVAsyncClient* ad_retrieval_client,
                KVAsyncClient* kv_async_client) {
        DCHECK(runtime_config.is_protected_app_signals_enabled);
        auto generate_bids_reactor =
            std::make_unique<ProtectedAppSignalsGenerateBidsReactor>(
                context, client, runtime_config, request, response,
                key_fetcher_manager, crypto_client, ad_retrieval_client,
                kv_async_client);
        return generate_bids_reactor.release();
      };

  BiddingService bidding_service(
      std::move(generate_bids_reactor_factory),
      CreateKeyFetcherManager(config_client,
                              enable_protected_app_signals
                                  ? CreatePublicKeyFetcher(config_client)
                                  : nullptr),
      CreateCryptoClient(), std::move(runtime_config),
      std::move(protected_app_signals_generate_bids_reactor_factory));

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  // This server is expected to accept insecure connections as it will be
  // deployed behind an HTTPS load balancer that terminates TLS.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

  if (config_client.HasParameter(HEALTHCHECK_PORT)) {
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
  PS_LOG(INFO) << "Server listening on " << server_address;
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
