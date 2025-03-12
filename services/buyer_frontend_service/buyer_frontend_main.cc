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

#include <memory>
#include <string>

#include <aws/core/Aws.h>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "services/buyer_frontend_service/buyer_frontend_service.h"
#include "services/buyer_frontend_service/providers/http_bidding_signals_async_provider.h"
#include "services/buyer_frontend_service/runtime_flags.h"
#include "services/common/clients/bidding_server/bidding_async_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/clients/http_kv_server/buyer/buyer_key_value_async_http_client.h"
#include "services/common/clients/http_kv_server/buyer/fake_buyer_key_value_async_http_client.h"
#include "services/common/constants/common_constants.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/feature_flags.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "services/common/util/signal_handler.h"
#include "services/common/util/tcmalloc_utils.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/public/cpio/interface/cpio.h"
#include "src/util/rlimit_core_config.h"
#include "src/util/status_macro/status_macros.h"

ABSL_FLAG(std::optional<uint16_t>, port, std::nullopt,
          "Port the server is listening on.");
ABSL_FLAG(std::optional<uint16_t>, healthcheck_port, std::nullopt,
          "Non-TLS port dedicated to healthchecks. Must differ from --port.");

ABSL_FLAG(std::optional<std::string>, bidding_server_addr, std::nullopt,
          "Bidding Server Address");
ABSL_FLAG(std::optional<std::string>, grpc_arg_default_authority, std::nullopt,
          "Domain for bidding server, or other domain for TLS Authority");
ABSL_FLAG(std::optional<std::string>, buyer_kv_server_addr, std::nullopt,
          "Buyer KV Server Address");
ABSL_FLAG(std::optional<std::string>, buyer_tkv_v2_server_addr, std::nullopt,
          "Buyer Trusted KV Server Address");
// Added for performance/benchmark testing of both types of http clients.
ABSL_FLAG(std::optional<int>, generate_bid_timeout_ms, std::nullopt,
          "Max time to wait for generate bid request to finish.");
ABSL_FLAG(std::optional<int>, protected_app_signals_generate_bid_timeout_ms,
          std::nullopt,
          "Max time to wait for protected app signals generate bid request to "
          "finish.");
ABSL_FLAG(std::optional<int>, bidding_signals_load_timeout_ms, std::nullopt,
          "Max time to wait for fetching bidding signals to finish.");
ABSL_FLAG(std::optional<bool>, enable_buyer_frontend_benchmarking, std::nullopt,
          "Enable benchmarking the BuyerFrontEnd Server.");
ABSL_FLAG(
    std::optional<bool>, create_new_event_engine, std::nullopt,
    "Share the event engine with gprc when false , otherwise create new one");
ABSL_FLAG(std::optional<bool>, enable_bidding_compression, std::nullopt,
          "Flag to enable bidding client compression. Turned off by default.");
ABSL_FLAG(std::optional<bool>, bfe_ingress_tls, std::nullopt,
          "If true, frontend gRPC service terminates TLS");
ABSL_FLAG(std::optional<std::string>, bfe_tls_key, std::nullopt,
          "TLS key string. Required if bfe_ingress_tls=true.");
ABSL_FLAG(std::optional<std::string>, bfe_tls_cert, std::nullopt,
          "TLS cert string. Required if bfe_ingress_tls=true.");
ABSL_FLAG(std::optional<bool>, bidding_egress_tls, std::nullopt,
          "If true, bidding service gRPC client uses TLS.");
ABSL_FLAG(
    bool, init_config_client, false,
    "Initialize config client to fetch any runtime flags not supplied from"
    " command line from cloud metadata store. False by default.");
ABSL_FLAG(std::optional<int64_t>,
          bfe_tcmalloc_background_release_rate_bytes_per_second, std::nullopt,
          "Amount of cached memory in bytes that is returned back to the "
          "system per second");
ABSL_FLAG(std::optional<int64_t>, bfe_tcmalloc_max_total_thread_cache_bytes,
          std::nullopt,
          "Maximum amount of cached memory in bytes across all threads (or "
          "logical CPUs)");
ABSL_FLAG(std::optional<bool>, propagate_buyer_signals_to_tkv, std::nullopt,
          "Propagate buyer signals to the key value server. Only works for v2");
ABSL_FLAG(
    std::optional<bool>, enable_hybrid, std::nullopt,
    "Enable hybrid. Only works for Chrome. Does not work for Android or PAS");
ABSL_FLAG(
    std::optional<std::string>, bidding_signals_fetch_mode, std::nullopt,
    "Specifies whether KV lookup for bidding signals is made or not, and if "
    "so, whether bidding signals are required for generating bids or not.");

namespace privacy_sandbox::bidding_auction_servers {

using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::grpc::Server;
using ::grpc::ServerBuilder;

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    absl::string_view config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_https_fetch_skips_tls_verification,
                        HTTPS_FETCH_SKIPS_TLS_VERIFICATION);
  config_client.SetFlag(FLAGS_healthcheck_port, HEALTHCHECK_PORT);
  config_client.SetFlag(FLAGS_bidding_server_addr, BIDDING_SERVER_ADDR);
  config_client.SetFlag(FLAGS_grpc_arg_default_authority,
                        GRPC_ARG_DEFAULT_AUTHORITY_VAL);
  config_client.SetFlag(FLAGS_buyer_kv_server_addr, BUYER_KV_SERVER_ADDR);
  config_client.SetFlag(FLAGS_buyer_tkv_v2_server_addr,
                        BUYER_TKV_V2_SERVER_ADDR);
  config_client.SetFlag(FLAGS_generate_bid_timeout_ms, GENERATE_BID_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_protected_app_signals_generate_bid_timeout_ms,
                        PROTECTED_APP_SIGNALS_GENERATE_BID_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_bidding_signals_load_timeout_ms,
                        BIDDING_SIGNALS_LOAD_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_enable_buyer_frontend_benchmarking,
                        ENABLE_BUYER_FRONTEND_BENCHMARKING);
  config_client.SetFlag(FLAGS_create_new_event_engine, CREATE_NEW_EVENT_ENGINE);
  config_client.SetFlag(FLAGS_enable_bidding_compression,
                        ENABLE_BIDDING_COMPRESSION);
  config_client.SetFlag(FLAGS_bfe_ingress_tls, BFE_INGRESS_TLS);
  config_client.SetFlag(FLAGS_bfe_tls_key, BFE_TLS_KEY);
  config_client.SetFlag(FLAGS_bfe_tls_cert, BFE_TLS_CERT);
  config_client.SetFlag(FLAGS_bidding_egress_tls, BIDDING_EGRESS_TLS);
  config_client.SetFlag(FLAGS_test_mode, TEST_MODE);
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
  config_client.SetFlag(FLAGS_consented_debug_token, CONSENTED_DEBUG_TOKEN);
  config_client.SetFlag(FLAGS_enable_otel_based_logging,
                        ENABLE_OTEL_BASED_LOGGING);
  config_client.SetFlag(FLAGS_enable_protected_app_signals,
                        ENABLE_PROTECTED_APP_SIGNALS);
  config_client.SetFlag(FLAGS_enable_protected_audience,
                        ENABLE_PROTECTED_AUDIENCE);
  config_client.SetFlag(FLAGS_ps_verbosity, PS_VERBOSITY);
  config_client.SetFlag(
      FLAGS_bfe_tcmalloc_background_release_rate_bytes_per_second,
      BFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND);
  config_client.SetFlag(FLAGS_bfe_tcmalloc_max_total_thread_cache_bytes,
                        BFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES);
  config_client.SetFlag(FLAGS_enable_chaffing, ENABLE_CHAFFING);
  config_client.SetFlag(FLAGS_debug_sample_rate_micro, DEBUG_SAMPLE_RATE_MICRO);
  config_client.SetFlag(FLAGS_enable_tkv_v2_browser, ENABLE_TKV_V2_BROWSER);
  config_client.SetFlag(FLAGS_tkv_egress_tls, TKV_EGRESS_TLS);
  config_client.SetFlag(FLAGS_propagate_buyer_signals_to_tkv,
                        PROPAGATE_BUYER_SIGNALS_TO_TKV);
  config_client.SetFlag(FLAGS_consent_all_requests, CONSENT_ALL_REQUESTS);
  config_client.SetFlag(FLAGS_enable_priority_vector, ENABLE_PRIORITY_VECTOR);
  config_client.SetFlag(FLAGS_enable_hybrid, ENABLE_HYBRID);
  config_client.SetFlag(FLAGS_bidding_signals_fetch_mode,
                        BIDDING_SIGNALS_FETCH_MODE);
  config_client.SetFlag(FLAGS_curlmopt_maxconnects, CURLMOPT_MAXCONNECTS);
  config_client.SetFlag(FLAGS_curlmopt_max_total_connections,
                        CURLMOPT_MAX_TOTAL_CONNECTIONS);
  config_client.SetFlag(FLAGS_curlmopt_max_host_connections,
                        CURLMOPT_MAX_HOST_CONNECTIONS);

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
  PS_LOG(INFO) << "Successfully constructed the config client.\n";

  return config_client;
}

// Brings up the gRPC async BuyerFrontEndService on FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));
  // InitTelemetry right after config_client being initialized
  InitTelemetry<GetBidsRequest>(config_util, config_client, metric::kBfe);
  PS_LOG(INFO, SystemLogContext()) << "server parameters:\n"
                                   << config_client.DebugString();

  // SetOverride after InitTelemetry, so it can log with SystemLogContext()
  // For security reasons, chaffing must always be enabled in a prod build.
#if (PS_IS_PROD_BUILD)
  config_client.SetOverride(kTrue, ENABLE_CHAFFING);
#endif

  int rate_micro = config_client.GetIntParameter(DEBUG_SAMPLE_RATE_MICRO);
  if (rate_micro < 0 || rate_micro > 1'000'000) {
    config_client.SetOverride("0", DEBUG_SAMPLE_RATE_MICRO);
  }
  MaySetBackgroundReleaseRate(config_client.GetInt64Parameter(
      BFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND));
  MaySetMaxTotalThreadCacheBytes(config_client.GetInt64Parameter(
      BFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES));

  int port = config_client.GetIntParameter(PORT);
  std::string bidding_server_addr =
      std::string(config_client.GetStringParameter(BIDDING_SERVER_ADDR));
  std::string grpc_arg_default_authority = std::string(
      config_client.GetStringParameter(GRPC_ARG_DEFAULT_AUTHORITY_VAL));
  std::string buyer_kv_server_addr =
      std::string(config_client.GetStringParameter(BUYER_KV_SERVER_ADDR));
  std::string buyer_tkv_v2_server_addr =
      std::string(config_client.GetStringParameter(BUYER_TKV_V2_SERVER_ADDR));
  bool use_tkv_v2_browser =
      config_client.GetBooleanParameter(ENABLE_TKV_V2_BROWSER);
  bool enable_buyer_frontend_benchmarking =
      config_client.GetBooleanParameter(ENABLE_BUYER_FRONTEND_BENCHMARKING);
  bool enable_bidding_compression =
      config_client.GetBooleanParameter(ENABLE_BIDDING_COMPRESSION);

  if (bidding_server_addr.empty()) {
    return absl::InvalidArgumentError("Missing: Bidding server address");
  }
  BiddingSignalsFetchMode bidding_signals_fetch_mode =
      BiddingSignalsFetchMode::REQUIRED;
  if (config_client.GetStringParameter(BIDDING_SIGNALS_FETCH_MODE) ==
      kSignalsFetchedButOptional) {
    bidding_signals_fetch_mode = BiddingSignalsFetchMode::FETCHED_BUT_OPTIONAL;
  } else if (config_client.GetStringParameter(BIDDING_SIGNALS_FETCH_MODE) ==
             kSignalsNotFetched) {
    bidding_signals_fetch_mode = BiddingSignalsFetchMode::NOT_FETCHED;
  }

  if (buyer_tkv_v2_server_addr == kIgnoredPlaceholderValue) {
    buyer_tkv_v2_server_addr = "";
  }
  if (buyer_kv_server_addr == kIgnoredPlaceholderValue) {
    buyer_kv_server_addr = "";
  }
  bool tkv_invalid = use_tkv_v2_browser && buyer_tkv_v2_server_addr.empty();
  bool http_kv_invalid = !use_tkv_v2_browser && buyer_kv_server_addr.empty();
  if (bidding_signals_fetch_mode != BiddingSignalsFetchMode::NOT_FETCHED &&
      (tkv_invalid || http_kv_invalid)) {
    return absl::InvalidArgumentError(
        "The server is configured to perform the trusted bidding signals fetch "
        "(which is the default) yet no host has been provided, BYOS or Trusted "
        "KV.");
  }

  server_common::GrpcInit gprc_init;
  auto executor = std::make_unique<server_common::EventEngineExecutor>(
      config_client.GetBooleanParameter(CREATE_NEW_EVENT_ENGINE)
          ? grpc_event_engine::experimental::CreateEventEngine()
          : grpc_event_engine::experimental::GetDefaultEventEngine());
  std::unique_ptr<AsyncClient<GetBuyerValuesInput, GetBuyerValuesOutput>>
      buyer_kv_async_http_client;
  // we're moving away from the async providers and
  // the http client it points to above,
  // and the propagation of that member to the downstream components
  // we're moving away to the kv client below.
  // Keeping it here for migration purposes.
  std::unique_ptr<HttpBiddingSignalsAsyncProvider>
      bidding_signals_async_providers;
  std::unique_ptr<KVAsyncClient> kv_async_client;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager = CreateKeyFetcherManager(
          config_client, CreatePublicKeyFetcher(config_client));
  if (buyer_kv_server_addr == "E2E_TEST_MODE") {
    buyer_kv_async_http_client =
        std::make_unique<FakeBuyerKeyValueAsyncHttpClient>(
            buyer_kv_server_addr);
    bidding_signals_async_providers =
        std::make_unique<HttpBiddingSignalsAsyncProvider>(
            std::move(buyer_kv_async_http_client));
  } else if (!use_tkv_v2_browser) {
    buyer_kv_async_http_client = std::make_unique<BuyerKeyValueAsyncHttpClient>(
        buyer_kv_server_addr,
        std::make_unique<MultiCurlHttpFetcherAsync>(
            executor.get(),
            MultiCurlHttpFetcherAsyncOptions{
                .curlmopt_maxconnects =
                    config_client.GetIntParameter(CURLMOPT_MAXCONNECTS),
                .curlmopt_max_total_connections = config_client.GetIntParameter(
                    CURLMOPT_MAX_TOTAL_CONNECTIONS),
                .curlmopt_max_host_connections = config_client.GetIntParameter(
                    CURLMOPT_MAX_HOST_CONNECTIONS)}),
        true);
    bidding_signals_async_providers =
        std::make_unique<HttpBiddingSignalsAsyncProvider>(
            std::move(buyer_kv_async_http_client));
  } else {
    PS_LOG(INFO, SystemLogContext()) << "Using TKV V2 for browser traffic";
  }

  if (!buyer_tkv_v2_server_addr.empty()) {
    auto kv_v2_stub = kv_server::v2::KeyValueService::NewStub(CreateChannel(
        buyer_tkv_v2_server_addr,
        /*compression=*/true,
        /*secure=*/config_client.GetBooleanParameter(TKV_EGRESS_TLS),
        /*grpc_arg_default_authority=*/grpc_arg_default_authority));
    kv_async_client = std::make_unique<KVAsyncGrpcClient>(
        key_fetcher_manager.get(), std::move(kv_v2_stub));
  } else {
    PS_LOG(WARNING, SystemLogContext())
        << "TKV V2 endpoint not set. All CLIENT_TYPE_ANDROID "
           "protected audience requests will fail.";
    if (use_tkv_v2_browser) {
      PS_LOG(WARNING, SystemLogContext())
          << " TKV V2 endpoint not set, but ENABLE_TKV_V2_BROWSER is true. "
             "All CLIENT_TYPE_BROWSER requests will fail.";
    }
  }

  BuyerFrontEndService buyer_frontend_service(
      std::move(bidding_signals_async_providers),
      BiddingServiceClientConfig{
          .server_addr = bidding_server_addr,
          .compression = enable_bidding_compression,
          .secure_client =
              config_client.GetBooleanParameter(BIDDING_EGRESS_TLS),
          .is_pas_enabled =
              config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS),
          .grpc_arg_default_authority = grpc_arg_default_authority},
      std::move(key_fetcher_manager), CreateCryptoClient(),
      std::move(kv_async_client),
      GetBidsConfig{
          config_client.GetIntParameter(GENERATE_BID_TIMEOUT_MS),
          config_client.GetIntParameter(BIDDING_SIGNALS_LOAD_TIMEOUT_MS),
          config_client.GetIntParameter(
              PROTECTED_APP_SIGNALS_GENERATE_BID_TIMEOUT_MS),
          config_client.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS),
          config_client.GetBooleanParameter(ENABLE_PROTECTED_AUDIENCE),
          config_client.GetBooleanParameter(ENABLE_CHAFFING),
          config_client.GetBooleanParameter(ENABLE_TKV_V2_BROWSER),
          absl::GetFlag(FLAGS_enable_cancellation),
          absl::GetFlag(FLAGS_enable_kanon),
          config_client.GetIntParameter(DEBUG_SAMPLE_RATE_MICRO),
          config_client.GetBooleanParameter(CONSENT_ALL_REQUESTS),
          config_client.GetBooleanParameter(ENABLE_PRIORITY_VECTOR),
          config_client.GetBooleanParameter(TEST_MODE),
          buyer_tkv_v2_server_addr.empty(), bidding_signals_fetch_mode,
          config_client.GetBooleanParameter(PROPAGATE_BUYER_SIGNALS_TO_TKV),
          config_client.GetBooleanParameter(ENABLE_HYBRID)},
      *executor, enable_buyer_frontend_benchmarking);

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  std::string server_address = absl::StrCat("0.0.0.0:", port);
  if (config_client.GetBooleanParameter(BFE_INGRESS_TLS)) {
    std::vector<grpc::experimental::IdentityKeyCertPair> key_cert_pairs{{
        .private_key =
            std::string(config_client.GetStringParameter(BFE_TLS_KEY)),
        .certificate_chain =
            std::string(config_client.GetStringParameter(BFE_TLS_CERT)),
    }};
    auto certificate_provider =
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            key_cert_pairs);
    grpc::experimental::TlsServerCredentialsOptions options(
        certificate_provider);
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("buyer_frontend");
    builder.AddListeningPort(server_address,
                             grpc::experimental::TlsServerCredentials(options));
  } else {
    // Listen on the given address without any authentication mechanism.
    // This server is expected to accept insecure connections as it will be
    // deployed behind an HTTPS load balancer that terminates TLS.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  }

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
  // Set soft limit of metadata size to 64 KB.
  builder.AddChannelArgument(GRPC_ARG_MAX_METADATA_SIZE, 64L * 1024L);
  builder.RegisterService(&buyer_frontend_service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    return absl::UnavailableError("Error starting Server.");
  }
  PS_LOG(INFO, SystemLogContext()) << "Server listening on " << server_address;

  // Wait for the server to shut down. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers

int main(int argc, char** argv) {
  privacy_sandbox::bidding_auction_servers::RegisterCommonSignalHandlers();
  absl::InitializeSymbolizer(argv[0]);
  privacysandbox::server_common::SetRLimits({
      .enable_core_dumps = PS_ENABLE_CORE_DUMPS,
  });
  absl::FailureSignalHandlerOptions options = {.call_previous_handler = true};
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
      << "Failed to run server ";

  if (init_config_client) {
    google::scp::cpio::Cpio::ShutdownCpio(cpio_options);
  }

  return 0;
}
