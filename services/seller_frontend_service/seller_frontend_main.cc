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

#include <iostream>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "google/protobuf/text_format.h"
#include "grpcpp/ext/proto_server_reflection_plugin.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/health_check_service_interface.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "services/common/util/tcmalloc_utils.h"
#include "services/seller_frontend_service/runtime_flags.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/util/key_fetcher_utils.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/public/cpio/interface/cpio.h"
#include "src/util/rlimit_core_config.h"
#include "src/util/status_macro/status_macros.h"

ABSL_FLAG(std::optional<uint16_t>, port, std::nullopt,
          "Port the server is listening on.");
ABSL_FLAG(std::optional<uint16_t>, healthcheck_port, std::nullopt,
          "Non-TLS port dedicated to healthchecks. Must differ from --port.");
ABSL_FLAG(std::optional<uint16_t>, get_bid_rpc_timeout_ms, std::nullopt, "");
ABSL_FLAG(std::optional<uint16_t>, key_value_signals_fetch_rpc_timeout_ms,
          std::nullopt, "Timeout for a GetBid gRPC request");
ABSL_FLAG(std::optional<uint16_t>, score_ads_rpc_timeout_ms, std::nullopt,
          "Timeout for a Key-Value GetValues gRPC request.");
ABSL_FLAG(
    std::optional<std::string>, seller_origin_domain, std::nullopt,
    "Domain of this seller. Incoming requests are validated against this.");
ABSL_FLAG(std::optional<std::string>, auction_server_host, std::nullopt,
          "Domain address of the auction server used for ad scoring.");
ABSL_FLAG(std::optional<std::string>, grpc_arg_default_authority, std::nullopt,
          "Authority for auction server name, see "
          "https://www.rfc-editor.org/rfc/rfc3986#section-3.2");
ABSL_FLAG(std::optional<std::string>, key_value_signals_host, std::nullopt,
          "Domain address of the Key-Value server for the scoring signals.");
ABSL_FLAG(std::optional<std::string>, buyer_server_hosts, std::nullopt,
          "Comma seperated list of domain addresses of the BuyerFrontEnd "
          "services for getting bids.");
ABSL_FLAG(std::optional<bool>, enable_buyer_compression, std::nullopt,
          "Flag to enable buyer client compression. Turned off by default.");
ABSL_FLAG(std::optional<bool>, enable_auction_compression, std::nullopt,
          "Flag to enable auction client compression. Turned off by default.");
ABSL_FLAG(std::optional<bool>, enable_seller_frontend_benchmarking,
          std::nullopt, "Flag to enable benchmarking.");
ABSL_FLAG(
    std::optional<bool>, create_new_event_engine, std::nullopt,
    "Share the event engine with gprc when false , otherwise create new one");
ABSL_FLAG(
    bool, init_config_client, false,
    "Initialize config client to fetch any runtime flags not supplied from"
    " command line from cloud metadata store. False by default.");
ABSL_FLAG(std::optional<bool>, sfe_ingress_tls, std::nullopt,
          "If true, frontend gRPC service terminates TLS");
ABSL_FLAG(std::optional<std::string>, sfe_tls_key, std::nullopt,
          "TLS key string. Required if sfe_ingress_tls=true.");
ABSL_FLAG(std::optional<std::string>, sfe_tls_cert, std::nullopt,
          "TLS cert string. Required if sfe_ingress_tls=true.");
ABSL_FLAG(std::optional<bool>, auction_egress_tls, std::nullopt,
          "If true, auction service gRPC client uses TLS.");
ABSL_FLAG(std::optional<bool>, buyer_egress_tls, std::nullopt,
          "If true, buyer frontend service gRPC clients uses TLS.");
ABSL_FLAG(std::optional<std::string>, sfe_public_keys_endpoints, std::nullopt,
          "Endpoints serving set of public keys used for encryption across the "
          "supported cloud platforms");
ABSL_FLAG(std::optional<std::string>, seller_cloud_platforms_map, std::nullopt,
          "Seller partner cloud platforms. Can be empty."
          "Enables requests to the getComponentAuctionCiphertexts RPC.");
ABSL_FLAG(std::optional<int64_t>,
          sfe_tcmalloc_background_release_rate_bytes_per_second, std::nullopt,
          "Amount of cached memory in bytes that is returned back to the "
          "system per second");
ABSL_FLAG(std::optional<int64_t>, sfe_tcmalloc_max_total_thread_cache_bytes,
          std::nullopt,
          "Maximum amount of cached memory in bytes across all threads (or "
          "logical CPUs)");

namespace privacy_sandbox::bidding_auction_servers {

using ::grpc::Server;
using ::grpc::ServerBuilder;

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    absl::string_view config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_healthcheck_port, HEALTHCHECK_PORT);
  config_client.SetFlag(FLAGS_get_bid_rpc_timeout_ms, GET_BID_RPC_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_key_value_signals_fetch_rpc_timeout_ms,
                        KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_score_ads_rpc_timeout_ms,
                        SCORE_ADS_RPC_TIMEOUT_MS);
  config_client.SetFlag(FLAGS_seller_origin_domain, SELLER_ORIGIN_DOMAIN);
  config_client.SetFlag(FLAGS_auction_server_host, AUCTION_SERVER_HOST);
  config_client.SetFlag(FLAGS_grpc_arg_default_authority,
                        GRPC_ARG_DEFAULT_AUTHORITY_VAL);
  config_client.SetFlag(FLAGS_key_value_signals_host, KEY_VALUE_SIGNALS_HOST);
  config_client.SetFlag(FLAGS_buyer_server_hosts, BUYER_SERVER_HOSTS);
  config_client.SetFlag(FLAGS_enable_buyer_compression,
                        ENABLE_BUYER_COMPRESSION);
  config_client.SetFlag(FLAGS_enable_auction_compression,
                        ENABLE_AUCTION_COMPRESSION);
  config_client.SetFlag(FLAGS_enable_seller_frontend_benchmarking,
                        ENABLE_SELLER_FRONTEND_BENCHMARKING);
  config_client.SetFlag(FLAGS_create_new_event_engine, CREATE_NEW_EVENT_ENGINE);
  config_client.SetFlag(FLAGS_sfe_ingress_tls, SFE_INGRESS_TLS);
  config_client.SetFlag(FLAGS_sfe_tls_key, SFE_TLS_KEY);
  config_client.SetFlag(FLAGS_sfe_tls_cert, SFE_TLS_CERT);
  config_client.SetFlag(FLAGS_auction_egress_tls, AUCTION_EGRESS_TLS);
  config_client.SetFlag(FLAGS_buyer_egress_tls, BUYER_EGRESS_TLS);

  config_client.SetFlag(FLAGS_test_mode, TEST_MODE);
  config_client.SetFlag(FLAGS_public_key_endpoint, PUBLIC_KEY_ENDPOINT);
  config_client.SetFlag(FLAGS_sfe_public_keys_endpoints,
                        SFE_PUBLIC_KEYS_ENDPOINTS);
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
  config_client.SetFlag(FLAGS_seller_cloud_platforms_map,
                        SELLER_CLOUD_PLATFORMS_MAP);
  config_client.SetFlag(
      FLAGS_sfe_tcmalloc_background_release_rate_bytes_per_second,
      SFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND);
  config_client.SetFlag(FLAGS_sfe_tcmalloc_max_total_thread_cache_bytes,
                        SFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES);
  config_client.SetFlag(FLAGS_enable_chaffing, ENABLE_CHAFFING);

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

// For security reasons, chaffing must always be enabled in a prod build.
#if (PS_IS_PROD_BUILD)
  config_client.SetOverride(kTrue, ENABLE_CHAFFING);
#endif

  return config_client;
}

// Brings up the gRPC SellerFrontEndService on the FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));
  // InitTelemetry right after config_client being initialized
  InitTelemetry<SelectAdRequest>(
      config_util, config_client, metric::kSfe,
      FetchIgOwnerList(ParseIgOwnerToBfeDomainMap(
          config_client.GetStringParameter(BUYER_SERVER_HOSTS))));
  PS_LOG(INFO, SystemLogContext()) << "server parameters:\n"
                                   << config_client.DebugString();

  MaySetBackgroundReleaseRate(config_client.GetInt64Parameter(
      SFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND));
  MaySetMaxTotalThreadCacheBytes(config_client.GetInt64Parameter(
      SFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES));

  std::string server_address =
      absl::StrCat("0.0.0.0:", config_client.GetStringParameter(PORT));
  server_common::GrpcInit gprc_init;

  PS_ASSIGN_OR_RETURN(std::unique_ptr<server_common::PublicKeyFetcherInterface>
                          public_key_fetcher,
                      CreateSfePublicKeyFetcher(config_client));
  SellerFrontEndService seller_frontend_service(
      &config_client,
      CreateKeyFetcherManager(config_client, std::move(public_key_fetcher)),
      CreateCryptoClient());
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;

  if (config_client.GetBooleanParameter(SFE_INGRESS_TLS)) {
    std::vector<grpc::experimental::IdentityKeyCertPair> key_cert_pairs{{
        .private_key =
            std::string(config_client.GetStringParameter(SFE_TLS_KEY)),
        .certificate_chain =
            std::string(config_client.GetStringParameter(SFE_TLS_CERT)),
    }};
    auto certificate_provider =
        std::make_shared<grpc::experimental::StaticDataCertificateProvider>(
            key_cert_pairs);
    grpc::experimental::TlsServerCredentialsOptions options(
        certificate_provider);
    options.watch_identity_key_cert_pairs();
    options.set_identity_cert_name("seller_frontend");
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
  builder.RegisterService(&seller_frontend_service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    return absl::UnavailableError("Error starting Server.");
  }

  PS_LOG(INFO, SystemLogContext()) << "Server listening on " << server_address;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
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

  bool init_config_client = absl::GetFlag(FLAGS_init_config_client);
  google::scp::cpio::CpioOptions cpio_options;
  cpio_options.log_option = google::scp::cpio::LogOption::kConsoleLog;

  if (init_config_client) {
    CHECK(google::scp::cpio::Cpio::InitCpio(cpio_options).Successful())
        << "Failed to initialize CPIO library.";
  }

  CHECK_OK(privacy_sandbox::bidding_auction_servers::RunServer())
      << "Failed to run server ";

  if (init_config_client) {
    CHECK(google::scp::cpio::Cpio::ShutdownCpio(cpio_options).Successful())
        << "Failed to shutdown CPIO library.";
  }

  return 0;
}
