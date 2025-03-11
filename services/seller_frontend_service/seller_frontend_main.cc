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
#include "services/auction_service/udf_fetcher/auction_code_fetch_config.pb.h"
#include "services/common/aliases.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/telemetry/configure_telemetry.h"
#include "services/common/util/signal_handler.h"
#include "services/common/util/tcmalloc_utils.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_interface.h"
#include "services/seller_frontend_service/report_win_map.h"
#include "services/seller_frontend_service/runtime_flags.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/util/key_fetcher_utils.h"
#include "src/concurrent/event_engine_executor.h"
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
ABSL_FLAG(
    std::optional<std::string>, trusted_key_value_v2_signals_host, std::nullopt,
    "Domain address of the Trusted Key-Value server for the scoring signals.");
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
ABSL_FLAG(std::optional<std::string>, k_anon_api_key, "",
          "API key used to query hashes from k-anon service. Required when "
          "k-anon is enabled.");
ABSL_FLAG(
    std::optional<bool>, allow_compressed_auction_config, false,
    "Enable reading of the compressed auction config field in SelectAdRequest");
ABSL_FLAG(
    std::optional<std::string>, scoring_signals_fetch_mode, std::nullopt,
    "Specifies whether KV lookup for scoring signals is made or not, and if "
    "so, whether scoring signals are required for scoring ads or not.");
ABSL_FLAG(std::optional<std::string>, header_passed_to_buyer, std::nullopt,
          "http headers in sfe request to be passed in bfe request, multiple "
          "headers in lower case separated by comma without space");
ABSL_FLAG(std::optional<int>, k_anon_total_num_hash, 1000,
          "Total number of entries to k-anon cache. Required when k-anon is "
          "enabled.");
ABSL_FLAG(std::optional<double>, expected_k_anon_to_non_k_anon_ratio, 1.0,
          "Expected ratio of k-anon to non-k-anon hashes used to size the "
          "caches. Required when k-anon is enabled.");
ABSL_FLAG(
    std::optional<int64_t>, k_anon_client_time_out_ms, 60000,
    "A time out for k-anon client query. Required when k-anon is enabled.");
ABSL_FLAG(std::optional<int>, num_k_anon_shards, 1,
          "Number of shards for cache storing k-anon hashes. Required when "
          "k-anon is enabled.");
ABSL_FLAG(std::optional<int>, num_non_k_anon_shards, 1,
          "Number of shards for cache storing non k-anon hashes. Required "
          "when k-anon is enabled.");
ABSL_FLAG(std::optional<int64_t>, test_mode_k_anon_cache_ttl_ms, 86400,
          "Configurable k-anon cache TTL in TEST_MODE. Set to be 24 hours "
          "otherwise.");
ABSL_FLAG(std::optional<int64_t>, test_mode_non_k_anon_cache_ttl_ms, 10800,
          "Configurable non k-anon cache TTL in TEST_MODE. Set to be 3 hours "
          "otherwise.");
ABSL_FLAG(std::optional<bool>, enable_k_anon_query_cache, true,
          "Flag to make k-anon cache query optional. If set to false, k-anon "
          "caches will not be queried.");
ABSL_FLAG(
    std::optional<bool>, enable_buyer_caching, std::nullopt,
    "Enable caching for which buyers are invoked for a particular request");

namespace privacy_sandbox::bidding_auction_servers {

// K-anon service address to use for querying the status of k-anon hashes.
inline constexpr char kKAnonServiceAddr[] = "kanonymityquery.googleapis.com";

ReportWinMap GetReportWinMapFromSellerCodeFetchConfig(
    const auction_service::SellerCodeFetchConfig& seller_code_fetch_config) {
  const auto& proto_buyer_report_win_js_urls =
      seller_code_fetch_config.buyer_report_win_js_urls();
  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls(
      proto_buyer_report_win_js_urls.begin(),
      proto_buyer_report_win_js_urls.end());

  const auto& proto_protected_app_signals_buyer_report_win_js_urls =
      seller_code_fetch_config.protected_app_signals_buyer_report_win_js_urls();
  absl::flat_hash_map<std::string, std::string>
      protected_app_signals_buyer_report_win_js_urls(
          proto_protected_app_signals_buyer_report_win_js_urls.begin(),
          proto_protected_app_signals_buyer_report_win_js_urls.end());
  return {
      .buyer_report_win_js_urls = std::move(buyer_report_win_js_urls),
      .protected_app_signals_buyer_report_win_js_urls =
          std::move(protected_app_signals_buyer_report_win_js_urls),
  };
}

KAnonCacheManagerConfig GetKAnonCacheManagerConfig(
    const TrustedServersConfigClient& config_client) {
  KAnonCacheManagerConfig config = {
      .total_num_hash = config_client.GetIntParameter(K_ANON_TOTAL_NUM_HASH),
      .expected_k_anon_to_non_k_anon_ratio =
          config_client.GetDoubleParameter(EXPECTED_K_ANON_TO_NON_K_ANON_RATIO),
      .client_time_out = absl::Milliseconds(
          config_client.GetInt64Parameter(K_ANON_CLIENT_TIME_OUT_MS)),
      .num_k_anon_shards = config_client.GetIntParameter(NUM_K_ANON_SHARDS),
      .num_non_k_anon_shards =
          config_client.GetIntParameter(NUM_NON_K_ANON_SHARDS),
      .enable_k_anon_cache =
          config_client.GetBooleanParameter(ENABLE_K_ANON_QUERY_CACHE)};

  if (config_client.GetBooleanParameter(TEST_MODE)) {
    config.k_anon_ttl = absl::Milliseconds(
        config_client.GetInt64Parameter(TEST_MODE_K_ANON_CACHE_TTL_MS));
    config.non_k_anon_ttl = absl::Milliseconds(
        config_client.GetInt64Parameter(TEST_MODE_NON_K_ANON_CACHE_TTL_MS));
  }
  return config;
}

using ::grpc::Server;
using ::grpc::ServerBuilder;

absl::StatusOr<TrustedServersConfigClient> GetConfigClient(
    absl::string_view config_param_prefix) {
  TrustedServersConfigClient config_client(GetServiceFlags());
  config_client.SetFlag(FLAGS_port, PORT);
  config_client.SetFlag(FLAGS_https_fetch_skips_tls_verification,
                        HTTPS_FETCH_SKIPS_TLS_VERIFICATION);
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
  config_client.SetFlag(FLAGS_seller_code_fetch_config,
                        SELLER_CODE_FETCH_CONFIG);
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
  config_client.SetFlag(FLAGS_k_anon_api_key, K_ANON_API_KEY);
  config_client.SetFlag(FLAGS_enable_chaffing, ENABLE_CHAFFING);
  config_client.SetFlag(FLAGS_debug_sample_rate_micro, DEBUG_SAMPLE_RATE_MICRO);
  config_client.SetFlag(FLAGS_enable_priority_vector, ENABLE_PRIORITY_VECTOR);
  config_client.SetFlag(FLAGS_consent_all_requests, CONSENT_ALL_REQUESTS);
  config_client.SetFlag(FLAGS_trusted_key_value_v2_signals_host,
                        TRUSTED_KEY_VALUE_V2_SIGNALS_HOST);
  config_client.SetFlag(FLAGS_enable_tkv_v2_browser, ENABLE_TKV_V2_BROWSER);
  config_client.SetFlag(FLAGS_tkv_egress_tls, TKV_EGRESS_TLS);
  config_client.SetFlag(FLAGS_allow_compressed_auction_config,
                        ALLOW_COMPRESSED_AUCTION_CONFIG);
  config_client.SetFlag(FLAGS_scoring_signals_fetch_mode,
                        SCORING_SIGNALS_FETCH_MODE);
  config_client.SetFlag(FLAGS_header_passed_to_buyer, HEADER_PASSED_TO_BUYER);
  config_client.SetFlag(FLAGS_k_anon_total_num_hash, K_ANON_TOTAL_NUM_HASH);
  config_client.SetFlag(FLAGS_expected_k_anon_to_non_k_anon_ratio,
                        EXPECTED_K_ANON_TO_NON_K_ANON_RATIO);
  config_client.SetFlag(FLAGS_k_anon_client_time_out_ms,
                        K_ANON_CLIENT_TIME_OUT_MS);
  config_client.SetFlag(FLAGS_num_k_anon_shards, NUM_K_ANON_SHARDS);
  config_client.SetFlag(FLAGS_num_non_k_anon_shards, NUM_NON_K_ANON_SHARDS);
  config_client.SetFlag(FLAGS_test_mode_k_anon_cache_ttl_ms,
                        TEST_MODE_K_ANON_CACHE_TTL_MS);
  config_client.SetFlag(FLAGS_test_mode_non_k_anon_cache_ttl_ms,
                        TEST_MODE_NON_K_ANON_CACHE_TTL_MS);
  config_client.SetFlag(FLAGS_enable_k_anon_query_cache,
                        ENABLE_K_ANON_QUERY_CACHE);
  config_client.SetFlag(FLAGS_enable_buyer_caching, ENABLE_BUYER_CACHING);
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

  std::string trusted_key_value_v2_signals_host = std::string(
      config_client.GetStringParameter(TRUSTED_KEY_VALUE_V2_SIGNALS_HOST));
  const bool use_tkv_v2_browser =
      config_client.GetBooleanParameter(ENABLE_TKV_V2_BROWSER);
  if (trusted_key_value_v2_signals_host == kIgnoredPlaceholderValue) {
    if (use_tkv_v2_browser) {
      return absl::InvalidArgumentError(
          "Missing: Seller Trusted KV server address");
    }
    PS_LOG(WARNING) << "TRUSTED_KEY_VALUE_V2_SIGNALS_HOST is set to "
                    << kIgnoredPlaceholderValue
                    << ", this can affect Android auctions";
    trusted_key_value_v2_signals_host = "";
  }

  std::string byos_key_value_signals_host =
      std::string(config_client.GetStringParameter(KEY_VALUE_SIGNALS_HOST));
  if (config_client.GetStringParameter(SCORING_SIGNALS_FETCH_MODE) !=
          kSignalsNotFetched &&
      (byos_key_value_signals_host.empty() ||
       byos_key_value_signals_host == kIgnoredPlaceholderValue) &&
      trusted_key_value_v2_signals_host.empty()) {
    return absl::InvalidArgumentError(
        "The server is configured to perform the trusted scoring signals fetch "
        "yet no host has been provided, BYOS or Trusted KV.");
  }

  PS_LOG(INFO) << "Successfully constructed the config client.";

  return config_client;
}

// Brings up the gRPC SellerFrontEndService on the FLAGS_port.
absl::Status RunServer() {
  TrustedServerConfigUtil config_util(absl::GetFlag(FLAGS_init_config_client));
  PS_ASSIGN_OR_RETURN(TrustedServersConfigClient config_client,
                      GetConfigClient(config_util.GetConfigParameterPrefix()));
  // InitTelemetry right after config_client being initialized
  // Do not log to SystemLogContext() before InitTelemetry
  const auto buyer_server_hosts_map = ParseIgOwnerToBfeDomainMap(
      config_client.GetStringParameter(BUYER_SERVER_HOSTS));
  ABSL_CHECK_OK(buyer_server_hosts_map)
      << "Error in fetching IG Owner to BFE domain map.";
  InitTelemetry<SelectAdRequest>(config_util, config_client, metric::kSfe,
                                 FetchIgOwnerList(*buyer_server_hosts_map));
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
      SFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND));
  MaySetMaxTotalThreadCacheBytes(config_client.GetInt64Parameter(
      SFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES));

  // Convert Json string into a AuctionCodeBlobFetcherConfig proto
  auction_service::SellerCodeFetchConfig code_fetch_proto;
  PS_RETURN_IF_ERROR(google::protobuf::util::JsonStringToMessage(
      config_client.GetStringParameter(SELLER_CODE_FETCH_CONFIG).data(),
      &code_fetch_proto));

  std::string server_address =
      absl::StrCat("0.0.0.0:", config_client.GetStringParameter(PORT));
  server_common::GrpcInit gprc_init;

  std::unique_ptr<KAnonCacheManagerInterface> k_anon_cache_manager = nullptr;
  std::unique_ptr<server_common::Executor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          config_client.GetBooleanParameter(CREATE_NEW_EVENT_ENGINE)
              ? grpc_event_engine::experimental::CreateEventEngine()
              : grpc_event_engine::experimental::GetDefaultEventEngine());
  if (absl::GetFlag(FLAGS_enable_kanon)) {
    PS_LOG(INFO) << "K-Anon is enabled on the service; instantiating the "
                    "k-anon cache manager";
    auto k_anon_client = std::make_unique<KAnonGrpcClient>(KAnonClientConfig{
        // TODO(b/383913428): We might want to make this configurable for
        // TEST_MODE.
        .server_addr = kKAnonServiceAddr,
        .api_key =
            std::string(config_client.GetStringParameter(K_ANON_API_KEY)),
        .compression = true,
        .secure_client = true,
    });
    k_anon_cache_manager = std::make_unique<KAnonCacheManager>(
        executor.get(), std::move(k_anon_client),
        GetKAnonCacheManagerConfig(config_client));
  }

  std::unique_ptr<Cache<std::string, InvokedBuyers>> invoked_buyers_cache;
  if (absl::GetFlag(FLAGS_enable_buyer_caching)) {
    static auto cache_stringify_func = [](const std::string& generation_id,
                                          const InvokedBuyers& invoked_buyers) {
      return absl::StrCat(
          "[generation_id: ", generation_id, ", invoked_buyers: [non_chaff: [",
          absl::StrJoin(invoked_buyers.non_chaff_buyer_names, ","),
          "], chaff: [", absl::StrJoin(invoked_buyers.chaff_buyer_names, ","),
          "]]]");
    };
    const int kCurrMaxQps = 1000;
    const int kInvokedBuyerCacheTtlSeconds = 60;
    invoked_buyers_cache = std::make_unique<Cache<std::string, InvokedBuyers>>(
        kInvokedBuyerCacheTtlSeconds * kCurrMaxQps, absl::Minutes(1),
        executor.get(), cache_stringify_func);
  }

  PS_ASSIGN_OR_RETURN(
      std::unique_ptr<server_common::PublicKeyFetcherInterface>
          public_key_fetcher,
      CreateSfePublicKeyFetcher(config_client, *buyer_server_hosts_map));
  SellerFrontEndService seller_frontend_service(
      &config_client,
      CreateKeyFetcherManager(config_client, std::move(public_key_fetcher)),
      CreateCryptoClient(),
      GetReportWinMapFromSellerCodeFetchConfig(code_fetch_proto),
      std::move(k_anon_cache_manager), std::move(invoked_buyers_cache));
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
  // Set soft limit of metadata size to 64 KB.
  builder.AddChannelArgument(GRPC_ARG_MAX_METADATA_SIZE, 64L * 1024L);
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
