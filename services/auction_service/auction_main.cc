
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

#include <aws/core/Aws.h>
#include <google/protobuf/util/json_util.h>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/data/runtime_config.h"
#include "services/auction_service/runtime_flags.h"
#include "services/auction_service/udf_fetcher/auction_code_fetch_config.pb.h"
#include "services/auction_service/udf_fetcher/seller_udf_fetch_manager.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "services/common/util/tcmalloc_utils.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/interface/cpio.h"
#include "src/util/rlimit_core_config.h"
#include "src/util/status_macro/status_macros.h"

ABSL_FLAG(std::optional<uint16_t>, port, std::nullopt,
          "Port the server is listening on.");
ABSL_FLAG(std::optional<uint16_t>, healthcheck_port, std::nullopt,
          "Non-TLS port dedicated to healthchecks. Must differ from --port.");
ABSL_FLAG(
    bool, init_config_client, false,
    "Initialize config client to fetch any runtime flags not supplied from"
    " command line from cloud metadata store. False by default.");
ABSL_FLAG(std::optional<bool>, enable_auction_service_benchmark, std::nullopt,
          "Benchmark the auction server and write the runtimes to the logs.");
ABSL_FLAG(
    std::optional<std::string>, seller_code_fetch_config, std::nullopt,
    "The JSON string for config fields necessary for AdTech code fetching.");
ABSL_FLAG(
    std::optional<std::int64_t>, udf_num_workers, std::nullopt,
    "The number of workers/threads for executing AdTech code in parallel.");
ABSL_FLAG(std::optional<std::int64_t>, js_worker_queue_len, std::nullopt,
          "The length of queue size for a single JS execution worker.");
ABSL_FLAG(std::optional<bool>, enable_report_win_input_noising, std::nullopt,
          "Enables noising and bucketing of the inputs to reportWin");
ABSL_FLAG(std::optional<int64_t>,
          auction_tcmalloc_background_release_rate_bytes_per_second,
          std::nullopt,
          "Amount of cached memory in bytes that is returned back to the "
          "system per second");
ABSL_FLAG(std::optional<int64_t>, auction_tcmalloc_max_total_thread_cache_bytes,
          std::nullopt,
          "Maximum amount of cached memory in bytes across all threads (or "
          "logical CPUs)");

namespace privacy_sandbox::bidding_auction_servers {

using ::google::scp::cpio::BlobStorageClientFactory;
using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::grpc::Server;
using ::grpc::ServerBuilder;

namespace {
bool ShouldEnableSellerAndBuyerUdfIsolation(bool test_mode) {
  if (test_mode) {
    return false;
  }
  return true;
}
}  // namespace

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    absl::string_view config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_healthcheck_port, HEALTHCHECK_PORT);
  config_client.SetFlag(FLAGS_enable_auction_service_benchmark,
                        ENABLE_AUCTION_SERVICE_BENCHMARK);
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
  config_client.SetFlag(FLAGS_seller_code_fetch_config,
                        SELLER_CODE_FETCH_CONFIG);
  config_client.SetFlag(FLAGS_udf_num_workers, UDF_NUM_WORKERS);
  config_client.SetFlag(FLAGS_js_worker_queue_len, JS_WORKER_QUEUE_LEN);
  config_client.SetFlag(FLAGS_enable_report_win_input_noising,
                        ENABLE_REPORT_WIN_INPUT_NOISING);
  config_client.SetFlag(FLAGS_consented_debug_token, CONSENTED_DEBUG_TOKEN);
  config_client.SetFlag(FLAGS_enable_otel_based_logging,
                        ENABLE_OTEL_BASED_LOGGING);
  config_client.SetFlag(FLAGS_enable_protected_app_signals,
                        ENABLE_PROTECTED_APP_SIGNALS);
  config_client.SetFlag(FLAGS_enable_protected_audience,
                        ENABLE_PROTECTED_AUDIENCE);
  config_client.SetFlag(FLAGS_ps_verbosity, PS_VERBOSITY);
  config_client.SetFlag(FLAGS_max_allowed_size_debug_url_bytes,
                        MAX_ALLOWED_SIZE_DEBUG_URL_BYTES);
  config_client.SetFlag(FLAGS_max_allowed_size_all_debug_urls_kb,
                        MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB);
  config_client.SetFlag(
      FLAGS_auction_tcmalloc_background_release_rate_bytes_per_second,
      AUCTION_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND);
  config_client.SetFlag(FLAGS_auction_tcmalloc_max_total_thread_cache_bytes,
                        AUCTION_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES);

  if (absl::GetFlag(FLAGS_init_config_client)) {
    PS_RETURN_IF_ERROR(config_client.Init(config_param_prefix)).LogError()
        << "Config client failed to initialize.";
  }
  // Set verbosity
  server_common::log::SetGlobalPSVLogLevel(
      config_client.GetIntParameter(PS_VERBOSITY));

  const bool enable_protected_audience =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_AUDIENCE);
  const bool enable_protected_app_signals =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  CHECK(enable_protected_audience || enable_protected_app_signals)
      << "Neither Protected Audience nor Protected App Signals support "
         "enabled.";
  PS_LOG(INFO) << "Protected Audience support enabled on the service: "
               << enable_protected_audience;
  PS_LOG(INFO) << "Protected App Signals support enabled on the service: "
               << enable_protected_app_signals;
  PS_LOG(INFO) << "Successfully constructed the config client.";
  return config_client;
}

// Brings up the gRPC AuctionService on FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));
  // InitTelemetry right after config_client being initialized
  InitTelemetry<ScoreAdsRequest>(config_util, config_client, metric::kAs);
  PS_LOG(INFO, SystemLogContext()) << "server parameters:\n"
                                   << config_client.DebugString();

  MaySetBackgroundReleaseRate(config_client.GetInt64Parameter(
      AUCTION_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND));
  MaySetMaxTotalThreadCacheBytes(config_client.GetInt64Parameter(
      AUCTION_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES));

  std::string_view port = config_client.GetStringParameter(PORT);
  std::string server_address = absl::StrCat("0.0.0.0:", port);

  CHECK(!config_client.GetStringParameter(SELLER_CODE_FETCH_CONFIG).empty())
      << "SELLER_CODE_FETCH_CONFIG is a mandatory flag.";

  auto dispatcher = V8Dispatcher([&config_client]() {
    DispatchConfig config;
    config.worker_queue_max_items =
        config_client.GetIntParameter(JS_WORKER_QUEUE_LEN);
    config.number_of_workers = config_client.GetIntParameter(UDF_NUM_WORKERS);
    return config;
  }());
  V8DispatchClient client(dispatcher);

  PS_RETURN_IF_ERROR(dispatcher.Init()) << "Could not start code dispatcher.";

  server_common::GrpcInit gprc_init;
  std::unique_ptr<server_common::Executor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());

  // Convert Json string into a AuctionCodeBlobFetcherConfig proto
  auction_service::SellerCodeFetchConfig code_fetch_proto;
  absl::Status result = google::protobuf::util::JsonStringToMessage(
      config_client.GetStringParameter(SELLER_CODE_FETCH_CONFIG).data(),
      &code_fetch_proto);
  CHECK(result.ok()) << "Could not parse SELLER_CODE_FETCH_CONFIG JsonString "
                        "to a proto message: "
                     << result;

  bool enable_seller_debug_url_generation =
      code_fetch_proto.enable_seller_debug_url_generation();
  bool enable_adtech_code_logging =
      code_fetch_proto.enable_adtech_code_logging();
  bool enable_report_result_url_generation =
      code_fetch_proto.enable_report_result_url_generation();
  bool enable_report_win_url_generation =
      code_fetch_proto.enable_report_win_url_generation();
  bool enable_private_aggregate_reporting =
      code_fetch_proto.enable_private_aggregate_reporting();
  const bool enable_protected_app_signals =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  bool test_mode = config_client.GetBooleanParameter(TEST_MODE);
  bool enable_seller_and_buyer_udf_isolation =
      ShouldEnableSellerAndBuyerUdfIsolation(test_mode);

  code_fetch_proto.set_enable_seller_and_buyer_udf_isolation(
      enable_seller_and_buyer_udf_isolation);
  MultiCurlHttpFetcherAsync http_fetcher =
      MultiCurlHttpFetcherAsync(executor.get());
  HttpFetcherAsync* seller_udf_fetcher = &http_fetcher;
  HttpFetcherAsync* buyer_reporting_udf_fetcher = &http_fetcher;
  // If protected app signals are not enabled, we will not score any PAS
  // bids, so it would not be required to fetch the PAS UDF.
  if (!enable_protected_app_signals) {
    code_fetch_proto.clear_protected_app_signals_buyer_report_win_js_urls();
  }
  SellerUdfFetchManager code_fetch_manager(
      BlobStorageClientFactory::Create(), executor.get(), seller_udf_fetcher,
      buyer_reporting_udf_fetcher, &dispatcher, code_fetch_proto,
      enable_protected_app_signals);
  PS_RETURN_IF_ERROR(code_fetch_manager.Init())
      << "Failed to initialize UDF fetch.";

  bool enable_auction_service_benchmark =
      config_client.GetBooleanParameter(ENABLE_AUCTION_SERVICE_BENCHMARK);

  // TODO(b/334909636) : AsyncReporter should not own HttpFetcher,
  // this needs to be decoupled so we can test different configurations.
  std::unique_ptr<AsyncReporter> async_reporter =
      std::make_unique<AsyncReporter>(
          std::make_unique<MultiCurlHttpFetcherAsync>(executor.get()));
  auto score_ads_reactor_factory =
      [&client, &async_reporter, enable_auction_service_benchmark](
          grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
          ScoreAdsResponse* response,
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
        return std::make_unique<ScoreAdsReactor>(
            context, client, request, response, std::move(benchmarkingLogger),
            key_fetcher_manager, crypto_client, async_reporter.get(),
            runtime_config);
      };

  std::string default_code_version =
      code_fetch_proto.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET
          ? code_fetch_proto.auction_js_bucket_default_blob()
          : kScoreAdBlobVersion;

  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_seller_debug_url_generation,
      .roma_timeout_ms =
          config_client.GetStringParameter(ROMA_TIMEOUT_MS).data(),
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_win_url_generation,
      .enable_protected_app_signals =
          config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS),
      .enable_report_win_input_noising =
          config_client.GetBooleanParameter(ENABLE_REPORT_WIN_INPUT_NOISING),
      .max_allowed_size_debug_url_bytes =
          config_client.GetIntParameter(MAX_ALLOWED_SIZE_DEBUG_URL_BYTES),
      .max_allowed_size_all_debug_urls_kb =
          config_client.GetIntParameter(MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB),
      .default_code_version = default_code_version,
      .enable_seller_and_buyer_udf_isolation =
          enable_seller_and_buyer_udf_isolation,
      .enable_private_aggregate_reporting = enable_private_aggregate_reporting};
  AuctionService auction_service(
      std::move(score_ads_reactor_factory),
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr),
      CreateCryptoClient(), std::move(runtime_config));

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
  builder.RegisterService(&auction_service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    return absl::UnavailableError("Error starting Server.");
  }
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  PS_LOG(INFO, SystemLogContext()) << "Server listening on " << server_address;
  server->Wait();
  // Ends periodic code blob fetching from an arbitrary url.
  PS_RETURN_IF_ERROR(code_fetch_manager.End())
      << "Error shutting down UDF fetcher.";
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
