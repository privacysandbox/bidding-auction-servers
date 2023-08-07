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

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "opentelemetry/metrics/provider.h"
#include "public/cpio/interface/cpio.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/bidding_code_fetch_config.pb.h"
#include "services/bidding_service/bidding_service.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/bidding_service/runtime_flags.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/code_fetch/periodic_code_fetcher.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/metric/server_definition.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "services/common/util/status_macros.h"
#include "src/cpp/concurrent/event_engine_executor.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"

ABSL_FLAG(std::optional<uint16_t>, port, std::nullopt,
          "Port the server is listening on.");
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
ABSL_FLAG(
    std::optional<std::int64_t>, js_worker_mem_mb, std::nullopt,
    "Shared memory used to store requests and responses shared between ROMA "
    "and JS workers. "
    "js_worker_mem_mb/js_worker_queue_len > average JS request size.");

namespace privacy_sandbox::bidding_auction_servers {

using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::grpc::Server;
using ::grpc::ServerBuilder;

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    std::string config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_enable_bidding_service_benchmark,
                        ENABLE_BIDDING_SERVICE_BENCHMARK);
  config_client.SetFlag(FLAGS_enable_encryption, ENABLE_ENCRYPTION);
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
  config_client.SetFlag(FLAGS_js_worker_mem_mb, JS_WORKER_MEM_MB);
  config_client.SetFlag(FLAGS_consented_debug_token, CONSENTED_DEBUG_TOKEN);
  config_client.SetFlag(FLAGS_enable_otel_based_logging,
                        ENABLE_OTEL_BASED_LOGGING);

  if (absl::GetFlag(FLAGS_init_config_client)) {
    PS_RETURN_IF_ERROR(config_client.Init(config_param_prefix)).LogError()
        << "Config client failed to initialize.";
  }

  VLOG(1) << "Successfully constructed the config client.";
  return config_client;
}

// Brings up the gRPC BiddingService on FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));

  std::string_view port = config_client.GetStringParameter(PORT);
  std::string server_address = absl::StrCat("0.0.0.0:", port);

  CHECK(!config_client.GetStringParameter(BUYER_CODE_FETCH_CONFIG).empty())
      << "BUYER_CODE_FETCH_CONFIG is a mandatory flag.";

  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  DispatchConfig config;
  config.ipc_memory_size_in_mb =
      config_client.GetIntParameter(JS_WORKER_MEM_MB);
  config.worker_queue_max_items =
      config_client.GetIntParameter(JS_WORKER_QUEUE_LEN);
  config.number_of_workers = config_client.GetIntParameter(JS_NUM_WORKERS);

  PS_RETURN_IF_ERROR(dispatcher.Init(config))
      << "Could not start code dispatcher.";

  server_common::GrpcInit gprc_init;
  std::unique_ptr<server_common::Executor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());
  std::unique_ptr<HttpFetcherAsync> http_fetcher =
      std::make_unique<MultiCurlHttpFetcherAsync>(executor.get());

  std::unique_ptr<CodeFetcherInterface> code_fetcher;

  // Convert Json string into a BiddingCodeBlobFetcherConfig proto
  bidding_service::BuyerCodeFetchConfig code_fetch_proto;
  absl::Status result = google::protobuf::util::JsonStringToMessage(
      config_client.GetStringParameter(BUYER_CODE_FETCH_CONFIG).data(),
      &code_fetch_proto);
  CHECK(result.ok()) << "Could not parse BUYER_CODE_FETCH_CONFIG JsonString to "
                        "a proto message.";

  bool enable_buyer_debug_url_generation =
      code_fetch_proto.enable_buyer_debug_url_generation();
  bool enable_buyer_code_wrapper = code_fetch_proto.enable_buyer_code_wrapper();
  bool enable_adtech_code_logging =
      code_fetch_proto.enable_adtech_code_logging();
  std::string js_url = code_fetch_proto.bidding_js_url();

  // Starts periodic code blob fetching from an arbitrary url only if js_url is
  // specified
  if (!js_url.empty()) {
    auto wrap_code = [enable_buyer_code_wrapper,
                      enable_buyer_debug_url_generation](
                         const std::vector<std::string>& adtech_code_blobs) {
      if (enable_buyer_code_wrapper) {
        return GetBuyerWrappedCode(adtech_code_blobs.at(0) /* js */,
                                   adtech_code_blobs.size() == 2
                                       ? adtech_code_blobs.at(1)
                                       : "" /* wasm */);
      }
      return adtech_code_blobs.at(0);
    };

    std::vector<std::string> endpoints = {js_url};
    if (!code_fetch_proto.bidding_wasm_helper_url().empty()) {
      endpoints = {js_url, code_fetch_proto.bidding_wasm_helper_url()};
    }

    code_fetcher = std::make_unique<PeriodicCodeFetcher>(
        endpoints, absl::Milliseconds(code_fetch_proto.url_fetch_period_ms()),
        std::move(http_fetcher), dispatcher, executor.get(),
        absl::Milliseconds(code_fetch_proto.url_fetch_timeout_ms()), wrap_code);

    code_fetcher->Start();
  } else if (!code_fetch_proto.bidding_js_path().empty()) {
    std::ifstream ifs(code_fetch_proto.bidding_js_path().data());
    std::string adtech_code_blob((std::istreambuf_iterator<char>(ifs)),
                                 (std::istreambuf_iterator<char>()));
    if (enable_buyer_code_wrapper) {
      adtech_code_blob = GetBuyerWrappedCode(adtech_code_blob, "");
    }

    PS_RETURN_IF_ERROR(dispatcher.LoadSync(1, adtech_code_blob))
        << "Could not load Adtech untrusted code for bidding.";
  } else {
    return absl::UnavailableError(
        "Code fetching config requires either a path or url.");
  }

  bool enable_bidding_service_benchmark =
      config_client.GetBooleanParameter(ENABLE_BIDDING_SERVICE_BENCHMARK);

  server_common::BuildDependentConfig telemetry_config(
      config_client
          .GetCustomParameter<server_common::TelemetryFlag>(TELEMETRY_CONFIG)
          .server_config);
  std::string collector_endpoint =
      config_client.GetStringParameter(COLLECTOR_ENDPOINT).data();
  server_common::InitTelemetry(
      config_util.GetService(), kOpenTelemetryVersion.data(),
      telemetry_config.TraceAllowed(), telemetry_config.MetricAllowed(),
      config_client.GetBooleanParameter(ENABLE_OTEL_BASED_LOGGING));
  server_common::ConfigureMetrics(CreateSharedAttributes(&config_util),
                                  CreateMetricsOptions(), collector_endpoint);
  server_common::ConfigureTracer(CreateSharedAttributes(&config_util),
                                 collector_endpoint);
  server_common::ConfigureLogger(CreateSharedAttributes(&config_util),
                                 collector_endpoint);
  AddSystemMetric(metric::BiddingContextMap(
      std::move(telemetry_config),
      opentelemetry::metrics::Provider::GetMeterProvider()
          ->GetMeter(config_util.GetService(), kOpenTelemetryVersion.data())
          .get()));

  auto generate_bids_reactor_factory =
      [&client, enable_bidding_service_benchmark](
          const GenerateBidsRequest* request, GenerateBidsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const BiddingServiceRuntimeConfig& runtime_config) {
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

  const BiddingServiceRuntimeConfig runtime_config = {
      .encryption_enabled =
          config_client.GetBooleanParameter(ENABLE_ENCRYPTION),
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .enable_buyer_code_wrapper = enable_buyer_code_wrapper,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .roma_timeout_ms =
          config_client.GetStringParameter(ROMA_TIMEOUT_MS).data()};

  BiddingService bidding_service(std::move(generate_bids_reactor_factory),
                                 CreateKeyFetcherManager(config_client),
                                 CreateCryptoClient(),
                                 std::move(runtime_config));

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  // This server is expected to accept insecure connections as it will be
  // deployed behind an HTTPS load balancer that terminates TLS.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&bidding_service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    return absl::UnavailableError("Error starting Server.");
  }
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  VLOG(1) << "Server listening on " << server_address;
  server->Wait();
  // Ends periodic code blob fetching from an arbitrary url.
  if (code_fetcher) {
    code_fetcher->End();
  }
  PS_RETURN_IF_ERROR(dispatcher.Stop())
      << "Error shutting down code dispatcher.";
  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  google::InitGoogleLogging(argv[0]);

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
