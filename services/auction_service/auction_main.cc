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

#include <aws/core/Aws.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "opentelemetry/metrics/provider.h"
#include "public/cpio/interface/cpio.h"
#include "services/auction_service/auction_adtech_code_wrapper.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/data/runtime_config.h"
#include "services/auction_service/runtime_flags.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/code_fetch/periodic_code_fetcher.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/metric/server_definition.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "services/common/util/status_macros.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/cpp/concurrent/event_engine_executor.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"

ABSL_FLAG(std::optional<uint16_t>, port, std::nullopt,
          "Port the server is listening on.");
// TODO(b/249682742): Make this a file until an updating design is established.
ABSL_FLAG(std::optional<std::string>, js_path, std::nullopt,
          "The javascript scoreAd script.");
ABSL_FLAG(
    bool, init_config_client, false,
    "Initialize config client to fetch any runtime flags not supplied from"
    " command line from cloud metadata store. False by default.");
ABSL_FLAG(std::optional<bool>, enable_auction_service_benchmark, std::nullopt,
          "Benchmark the auction server and write the runtimes to the logs.");
ABSL_FLAG(std::optional<bool>, enable_seller_debug_url_generation, std::nullopt,
          "Allow seller debug URL generation.");
ABSL_FLAG(std::optional<std::string>, js_url, std::nullopt,
          "The URL Endpoint for fetching AdTech code blob.");
ABSL_FLAG(
    std::optional<std::int64_t>, js_url_fetch_period_ms, std::nullopt,
    "Period of how often to fetch AdTech code blob from the URL endpoint.");
ABSL_FLAG(
    std::optional<std::int64_t>, js_time_out_ms, std::nullopt,
    "A time out limit for HttpsFetcherAsyc client to stop executing FetchUrl.");

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
  config_client.SetFlag(FLAGS_js_path, JS_PATH);
  config_client.SetFlag(FLAGS_enable_auction_service_benchmark,
                        ENABLE_AUCTION_SERVICE_BENCHMARK);
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
  config_client.SetFlag(FLAGS_primary_coordinator_region,
                        PRIMARY_COORDINATOR_REGION);
  config_client.SetFlag(FLAGS_secondary_coordinator_region,
                        SECONDARY_COORDINATOR_REGION);
  config_client.SetFlag(FLAGS_gcp_primary_workload_identity_pool_provider,
                        GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER);
  config_client.SetFlag(FLAGS_gcp_secondary_workload_identity_pool_provider,
                        GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER);
  config_client.SetFlag(FLAGS_gcp_primary_key_service_cloud_function_url,
                        GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL);
  config_client.SetFlag(FLAGS_gcp_secondary_key_service_cloud_function_url,
                        GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL);

  config_client.SetFlag(FLAGS_private_key_cache_ttl_seconds,
                        PRIVATE_KEY_CACHE_TTL_SECONDS);
  config_client.SetFlag(FLAGS_key_refresh_flow_run_frequency_seconds,
                        KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS);
  config_client.SetFlag(FLAGS_telemetry_config, TELEMETRY_CONFIG);
  config_client.SetFlag(FLAGS_enable_seller_debug_url_generation,
                        ENABLE_SELLER_DEBUG_URL_GENERATION);
  config_client.SetFlag(FLAGS_js_url, JS_URL);
  config_client.SetFlag(FLAGS_js_url_fetch_period_ms, JS_URL_FETCH_PERIOD_MS);
  config_client.SetFlag(FLAGS_js_time_out_ms, JS_TIME_OUT_MS);

  if (absl::GetFlag(FLAGS_init_config_client)) {
    PS_RETURN_IF_ERROR(config_client.Init(config_param_prefix)).LogError()
        << "Config client failed to initialize.";
  }

  VLOG(1) << "Successfully constructed the config client.";
  return config_client;
}

// Brings up the gRPC AuctionService on FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));

  std::string_view port = config_client.GetStringParameter(PORT);
  std::string server_address = absl::StrCat("0.0.0.0:", port);

  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  DispatchConfig config;
  PS_RETURN_IF_ERROR(dispatcher.Init(config))
      << "Could not start code dispatcher.";

  server_common::GrpcInit gprc_init;
  std::unique_ptr<server_common::Executor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());
  std::unique_ptr<HttpFetcherAsync> http_fetcher =
      std::make_unique<MultiCurlHttpFetcherAsync>(executor.get());

  bool enable_seller_debug_url_generation =
      config_client.GetBooleanParameter(ENABLE_SELLER_DEBUG_URL_GENERATION);

  std::unique_ptr<CodeFetcherInterface> code_fetcher;

  // Starts periodic code blob fetching from an arbitrary url only if js_url is
  // specified
  if (!config_client.GetStringParameter(JS_URL).empty()) {
    auto WrapCode = [enable_seller_debug_url_generation](
                        const std::string& adtech_code_blob) {
      return enable_seller_debug_url_generation
                 ? GetWrappedAdtechCodeForScoring(adtech_code_blob)
                 : adtech_code_blob;
    };

    code_fetcher = std::make_unique<PeriodicCodeFetcher>(
        config_client.GetStringParameter(JS_URL).data(),
        absl::Milliseconds(
            config_client.GetIntParameter(JS_URL_FETCH_PERIOD_MS)),
        std::move(http_fetcher), dispatcher, executor.get(),
        absl::Milliseconds(config_client.GetIntParameter(JS_TIME_OUT_MS)),
        WrapCode);

    code_fetcher->Start();
  } else {
    std::ifstream ifs(config_client.GetStringParameter(JS_PATH).data());
    std::string adtech_code_blob((std::istreambuf_iterator<char>(ifs)),
                                 (std::istreambuf_iterator<char>()));

    if (enable_seller_debug_url_generation) {
      adtech_code_blob = GetWrappedAdtechCodeForScoring(adtech_code_blob);
    }

    PS_RETURN_IF_ERROR(dispatcher.LoadSync(1, adtech_code_blob))
        << "Could not load Adtech untrusted code for scoring.";
  }

  bool enable_auction_service_benchmark =
      config_client.GetBooleanParameter(ENABLE_AUCTION_SERVICE_BENCHMARK);

  server_common::metric::BuildDependentConfig telemetry_config(
      config_client
          .GetCustomParameter<server_common::metric::TelemetryFlag>(
              TELEMETRY_CONFIG)
          .server_config);
  server_common::InitTelemetry(
      config_util.GetService(), kOpenTelemetryVersion.data(),
      telemetry_config.TraceAllowed(), telemetry_config.MetricAllowed());
  server_common::ConfigureMetrics(CreateSharedAttributes(&config_util),
                                  CreateMetricsOptions());
  server_common::ConfigureTracer(CreateSharedAttributes(&config_util));
  metric::AuctionContextMap(
      std::move(telemetry_config),
      opentelemetry::metrics::Provider::GetMeterProvider()
          ->GetMeter(config_util.GetService(), kOpenTelemetryVersion.data())
          .get());
  auto executer = std::make_unique<server_common::EventEngineExecutor>(
      grpc_event_engine::experimental::CreateEventEngine());
  auto score_ads_reactor_factory =
      [&client, &executer, enable_auction_service_benchmark](
          const ScoreAdsRequest* request, ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger;
        if (enable_auction_service_benchmark) {
          benchmarkingLogger = std::make_unique<ScoreAdsBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarkingLogger = std::make_unique<ScoreAdsNoOpLogger>();
        }
        std::unique_ptr<AsyncReporter> async_reporter =
            std::make_unique<AsyncReporter>(
                std::make_unique<MultiCurlHttpFetcherAsync>(executer.get()));
        return std::make_unique<ScoreAdsReactor>(
            client, request, response, std::move(benchmarkingLogger),
            key_fetcher_manager, crypto_client, std::move(async_reporter),
            runtime_config);
      };

  AuctionServiceRuntimeConfig runtime_config = {
      .encryption_enabled =
          config_client.GetBooleanParameter(ENABLE_ENCRYPTION),
      .enable_seller_debug_url_generation = enable_seller_debug_url_generation,
      .roma_timeout_ms =
          config_client.GetStringParameter(ROMA_TIMEOUT_MS).data()};
  AuctionService auction_service(std::move(score_ads_reactor_factory),
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
  builder.RegisterService(&auction_service);

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
