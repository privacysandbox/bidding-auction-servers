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
#include "services/bidding_service/constants.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/bidding_service/protected_app_signals_generate_bids_reactor.h"
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
#include "services/common/util/request_response_constants.h"
#include "services/common/util/signal_handler.h"
#include "src/cpp/concurrent/event_engine_executor.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"
#include "src/cpp/util/status_macro/status_macros.h"

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
ABSL_FLAG(std::optional<std::string>, ad_retrieval_server_kv_server_addr, "",
          "Ad Retrieval KV Server Address");
ABSL_FLAG(std::optional<bool>, byos_ad_retrieval_server, false,
          "Indicates whether or not the service is deployed in BYOS/dev "
          "only mode");
ABSL_FLAG(std::optional<int>, ad_retrieval_timeout_ms, std::nullopt,
          "The time in milliseconds to wait for the ads retrieval to complete");

namespace privacy_sandbox::bidding_auction_servers {

using bidding_service::BuyerCodeFetchConfig;
using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::grpc::Server;
using ::grpc::ServerBuilder;

constexpr int kMinNumCodeBlobs = 1;
constexpr int kMaxNumCodeBlobs = 2;

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    std::string config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_healthcheck_port, HEALTHCHECK_PORT);
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
  config_client.SetFlag(FLAGS_consented_debug_token, CONSENTED_DEBUG_TOKEN);
  config_client.SetFlag(FLAGS_enable_otel_based_logging,
                        ENABLE_OTEL_BASED_LOGGING);
  config_client.SetFlag(FLAGS_enable_protected_app_signals,
                        ENABLE_PROTECTED_APP_SIGNALS);
  config_client.SetFlag(FLAGS_ad_retrieval_server_kv_server_addr,
                        AD_RETRIEVAL_KV_SERVER_ADDR);
  config_client.SetFlag(FLAGS_byos_ad_retrieval_server,
                        BYOS_AD_RETRIEVAL_SERVER);
  config_client.SetFlag(FLAGS_ad_retrieval_timeout_ms, AD_RETRIEVAL_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_ps_verbosity, PS_VERBOSITY);

  // Set verbosity
  log::PS_VLOG_IS_ON(0, config_client.GetIntParameter(PS_VERBOSITY));

  if (absl::GetFlag(FLAGS_init_config_client)) {
    PS_RETURN_IF_ERROR(config_client.Init(config_param_prefix)).LogError()
        << "Config client failed to initialize.";
  }

  PS_VLOG(1) << "Protected App Signals support enabled for the service: "
             << config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  PS_VLOG(1) << "Successfully constructed the config client.";
  return config_client;
}

absl::StatusOr<std::unique_ptr<CodeFetcherInterface>> StartFetchingCodeBlob(
    const std::string& js_url, const std::string& wasm_helper_url,
    int roma_version, absl::string_view script_logging_name,
    absl::Duration url_fetch_period_ms, absl::Duration url_fetch_timeout_ms,
    absl::AnyInvocable<std::string(const std::vector<std::string>&)> wrap_code,
    const V8Dispatcher& dispatcher, server_common::Executor* executor) {
  if (js_url.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("JS URL for ", script_logging_name, " is missing"));
  }

  std::vector<std::string> endpoints = {js_url};
  if (!wasm_helper_url.empty()) {
    endpoints.emplace_back(wasm_helper_url);
  }
  auto code_fetcher = std::make_unique<PeriodicCodeFetcher>(
      std::move(endpoints), url_fetch_period_ms,
      std::make_unique<MultiCurlHttpFetcherAsync>(executor), dispatcher,
      executor, url_fetch_timeout_ms, std::move(wrap_code), roma_version);
  code_fetcher->Start();
  return code_fetcher;
}

absl::Status StartProtectedAppSignalsUdfFetching(
    const BuyerCodeFetchConfig& code_fetch_proto,
    const V8Dispatcher& dispatcher, server_common::Executor* executor,
    std::vector<std::unique_ptr<CodeFetcherInterface>>& code_fetchers) {
  absl::Duration url_fetch_period_ms =
      absl::Milliseconds(code_fetch_proto.url_fetch_period_ms());
  absl::Duration url_fetch_timeout_ms =
      absl::Milliseconds(code_fetch_proto.url_fetch_timeout_ms());
  PS_ASSIGN_OR_RETURN(
      auto bidding_code_fetcher,
      StartFetchingCodeBlob(
          code_fetch_proto.protected_app_signals_bidding_js_url(),
          code_fetch_proto.protected_app_signals_bidding_wasm_helper_url(),
          kProtectedAppSignalsGenerateBidBlobVersion,
          "protected_app_signals_bidding_js_url", url_fetch_period_ms,
          url_fetch_timeout_ms,
          [](const std::vector<std::string>& ad_tech_code_blobs) {
            DCHECK_GE(ad_tech_code_blobs.size(), kMinNumCodeBlobs);
            DCHECK_LE(ad_tech_code_blobs.size(), kMaxNumCodeBlobs);
            return GetBuyerWrappedCode(
                /*ad_tech_js=*/ad_tech_code_blobs[0],
                /*ad_tech_wasm=*/
                ad_tech_code_blobs.size() == 2 ? ad_tech_code_blobs[1] : "",
                AuctionType::kProtectedAppSignals,
                /*auction_specific_setup=*/"// No additional setup");
          },
          dispatcher, executor));

  code_fetchers.emplace_back(std::move(bidding_code_fetcher));
  PS_ASSIGN_OR_RETURN(
      auto prepare_data_for_ads_code_fetcher,
      StartFetchingCodeBlob(
          code_fetch_proto.prepare_data_for_ads_retrieval_js_url(),
          code_fetch_proto.prepare_data_for_ads_retrieval_wasm_helper_url(),
          kPrepareDataForAdRetrievalBlobVersion,
          "prepare_data_for_ads_retrieval_js_url", url_fetch_period_ms,
          url_fetch_timeout_ms,
          [](const std::vector<std::string>& ad_tech_code_blobs) {
            DCHECK_GE(ad_tech_code_blobs.size(), kMinNumCodeBlobs);
            DCHECK_LE(ad_tech_code_blobs.size(), kMaxNumCodeBlobs);
            return GetGenericBuyerWrappedCode(
                /*ad_tech_js=*/ad_tech_code_blobs[0],
                /*ad_tech_wasm=*/
                ad_tech_code_blobs.size() == 2 ? ad_tech_code_blobs[1] : "",
                kPrepareDataForAdRetrievalHandler,
                kPrepareDataForAdRetrievalArgs);
          },
          dispatcher, executor));
  code_fetchers.emplace_back(std::move(prepare_data_for_ads_code_fetcher));
  return absl::OkStatus();
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
  config.worker_queue_max_items =
      config_client.GetIntParameter(JS_WORKER_QUEUE_LEN);
  config.number_of_workers = config_client.GetIntParameter(JS_NUM_WORKERS);

  PS_RETURN_IF_ERROR(dispatcher.Init(config))
      << "Could not start code dispatcher.";

  server_common::GrpcInit gprc_init;
  std::unique_ptr<server_common::Executor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());

  std::vector<std::unique_ptr<CodeFetcherInterface>> code_fetchers;

  // Convert Json string into a BiddingCodeBlobFetcherConfig proto
  BuyerCodeFetchConfig code_fetch_proto;
  absl::Status result = google::protobuf::util::JsonStringToMessage(
      config_client.GetStringParameter(BUYER_CODE_FETCH_CONFIG).data(),
      &code_fetch_proto);
  CHECK(result.ok()) << "Could not parse BUYER_CODE_FETCH_CONFIG JsonString to "
                        "a proto message.";

  bool enable_buyer_debug_url_generation =
      code_fetch_proto.enable_buyer_debug_url_generation();
  bool enable_adtech_code_logging =
      code_fetch_proto.enable_adtech_code_logging();
  std::string js_url = code_fetch_proto.bidding_js_url();

  // Starts periodic code blob fetching from an arbitrary url only if js_url is
  // specified
  if (!js_url.empty()) {
    auto wrap_code = [](const std::vector<std::string>& adtech_code_blobs) {
      return GetBuyerWrappedCode(adtech_code_blobs.at(0) /* js */,
                                 adtech_code_blobs.size() == 2
                                     ? adtech_code_blobs.at(1)
                                     : "" /* wasm */,
                                 AuctionType::kProtectedAudience);
    };

    std::vector<std::string> endpoints = {js_url};
    if (!code_fetch_proto.bidding_wasm_helper_url().empty()) {
      endpoints = {js_url, code_fetch_proto.bidding_wasm_helper_url()};
    }

    auto code_fetcher = std::make_unique<PeriodicCodeFetcher>(
        std::move(endpoints),
        absl::Milliseconds(code_fetch_proto.url_fetch_period_ms()),
        std::make_unique<MultiCurlHttpFetcherAsync>(executor.get()), dispatcher,
        executor.get(),
        absl::Milliseconds(code_fetch_proto.url_fetch_timeout_ms()), wrap_code,
        kProtectedAudienceGenerateBidBlobVersion);

    code_fetcher->Start();
    code_fetchers.emplace_back(std::move(code_fetcher));
  } else if (!code_fetch_proto.bidding_js_path().empty()) {
    std::ifstream ifs(code_fetch_proto.bidding_js_path().data());
    std::string adtech_code_blob((std::istreambuf_iterator<char>(ifs)),
                                 (std::istreambuf_iterator<char>()));
    adtech_code_blob = GetBuyerWrappedCode(adtech_code_blob, "");

    PS_RETURN_IF_ERROR(dispatcher.LoadSync(
        kProtectedAudienceGenerateBidBlobVersion, adtech_code_blob))
        << "Could not load Adtech untrusted code for bidding.";
  } else {
    return absl::UnavailableError(
        "Code fetching config requires either a path or url.");
  }

  const bool enable_protected_app_signals =
      config_client.HasParameter(ENABLE_PROTECTED_APP_SIGNALS) &&
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  if (enable_protected_app_signals) {
    PS_RETURN_IF_ERROR(StartProtectedAppSignalsUdfFetching(
        code_fetch_proto, dispatcher, executor.get(), code_fetchers));
  }

  bool enable_bidding_service_benchmark =
      config_client.GetBooleanParameter(ENABLE_BIDDING_SERVICE_BENCHMARK);

  server_common::telemetry::BuildDependentConfig telemetry_config(
      config_client
          .GetCustomParameter<server_common::telemetry::TelemetryFlag>(
              TELEMETRY_CONFIG)
          .server_config);
  std::string collector_endpoint =
      config_client.GetStringParameter(COLLECTOR_ENDPOINT).data();
  server_common::InitTelemetry(
      config_util.GetService(), kOpenTelemetryVersion.data(),
      telemetry_config.TraceAllowed(), telemetry_config.MetricAllowed(),
      telemetry_config.LogsAllowed() &&
          config_client.GetBooleanParameter(ENABLE_OTEL_BASED_LOGGING));
  server_common::ConfigureTracer(CreateSharedAttributes(&config_util),
                                 collector_endpoint);
  server_common::ConfigureLogger(CreateSharedAttributes(&config_util),
                                 collector_endpoint);
  AddSystemMetric(metric::BiddingContextMap(
      telemetry_config,
      server_common::ConfigurePrivateMetrics(
          CreateSharedAttributes(&config_util),
          CreateMetricsOptions(telemetry_config.metric_export_interval_ms()),
          collector_endpoint),
      config_util.GetService(), kOpenTelemetryVersion.data()));

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

  const bool is_protected_app_signals_enabled =
      config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS);
  std::string ad_retrieval_server_kv_server_addr = std::string(
      config_client.GetStringParameter(AD_RETRIEVAL_KV_SERVER_ADDR));
  if (is_protected_app_signals_enabled &&
      ad_retrieval_server_kv_server_addr.empty()) {
    return absl::InvalidArgumentError("Missing: Ad Retrieval server address");
  }
  const bool byos_ad_retrieval_server =
      config_client.GetBooleanParameter(BYOS_AD_RETRIEVAL_SERVER);
  if (is_protected_app_signals_enabled && !byos_ad_retrieval_server) {
    return absl::InvalidArgumentError("Only BYOS mode is supported right now");
  }

  const BiddingServiceRuntimeConfig runtime_config = {
      .ad_retrieval_server_kv_server_addr =
          std::move(ad_retrieval_server_kv_server_addr),
      .encryption_enabled =
          config_client.GetBooleanParameter(ENABLE_ENCRYPTION),
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .roma_timeout_ms =
          config_client.GetStringParameter(ROMA_TIMEOUT_MS).data(),
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_otel_based_logging =
          config_client.GetBooleanParameter(ENABLE_OTEL_BASED_LOGGING),
      .consented_debug_token =
          std::string(config_client.GetStringParameter(CONSENTED_DEBUG_TOKEN)),
      .is_protected_app_signals_enabled = is_protected_app_signals_enabled,
      .ad_retrieval_timeout_ms =
          config_client.GetIntParameter(AD_RETRIEVAL_TIMEOUT_MS)};

  auto protected_app_signals_generate_bids_reactor_factory =
      [&client](const grpc::CallbackServerContext* context,
                const GenerateProtectedAppSignalsBidsRequest* request,
                const BiddingServiceRuntimeConfig& runtime_config,
                GenerateProtectedAppSignalsBidsResponse* response,
                server_common::KeyFetcherManagerInterface* key_fetcher_manager,
                CryptoClientWrapperInterface* crypto_client,
                AsyncClient<AdRetrievalInput, AdRetrievalOutput>*
                    http_ad_retrieval_async_client) {
        DCHECK(runtime_config.is_protected_app_signals_enabled);
        auto generate_bids_reactor =
            std::make_unique<ProtectedAppSignalsGenerateBidsReactor>(
                context, client, runtime_config, request, response,
                key_fetcher_manager, crypto_client,
                http_ad_retrieval_async_client);
        return generate_bids_reactor.release();
      };

  BiddingService bidding_service(
      std::move(generate_bids_reactor_factory),
      CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr),
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
  PS_VLOG(1) << "Server listening on " << server_address;
  server->Wait();
  // Ends periodic code blob fetching from an arbitrary url.
  for (const auto& code_fetcher : code_fetchers) {
    if (code_fetcher) {
      code_fetcher->End();
    }
  }
  PS_RETURN_IF_ERROR(dispatcher.Stop())
      << "Error shutting down code dispatcher.";
  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers

int main(int argc, char** argv) {
  signal(SIGSEGV, privacy_sandbox::bidding_auction_servers::SignalHandler);
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
