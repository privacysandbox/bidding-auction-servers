/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_CONSTANTS_COMMON_SERVICE_FLAGS_H_
#define SERVICES_COMMON_CONSTANTS_COMMON_SERVICE_FLAGS_H_

#include <string>
#include <vector>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "src/telemetry/flag/telemetry_flag.h"

ABSL_DECLARE_FLAG(std::optional<bool>, test_mode);
ABSL_DECLARE_FLAG(std::optional<bool>, https_fetch_skips_tls_verification);
ABSL_DECLARE_FLAG(std::optional<std::string>, public_key_endpoint);
ABSL_DECLARE_FLAG(std::optional<std::string>,
                  primary_coordinator_private_key_endpoint);
ABSL_DECLARE_FLAG(std::optional<std::string>,
                  secondary_coordinator_private_key_endpoint);
ABSL_DECLARE_FLAG(std::optional<std::string>,
                  primary_coordinator_account_identity);
ABSL_DECLARE_FLAG(std::optional<std::string>,
                  secondary_coordinator_account_identity);
ABSL_DECLARE_FLAG(std::optional<std::string>, primary_coordinator_region);
ABSL_DECLARE_FLAG(std::optional<std::string>, secondary_coordinator_region);
ABSL_DECLARE_FLAG(std::optional<std::string>,
                  gcp_primary_workload_identity_pool_provider);
ABSL_DECLARE_FLAG(std::optional<std::string>,
                  gcp_secondary_workload_identity_pool_provider);
ABSL_DECLARE_FLAG(std::optional<std::string>,
                  gcp_primary_key_service_cloud_function_url);
ABSL_DECLARE_FLAG(std::optional<std::string>,
                  gcp_secondary_key_service_cloud_function_url);
ABSL_DECLARE_FLAG(std::optional<int>, private_key_cache_ttl_seconds);
ABSL_DECLARE_FLAG(std::optional<int>, key_refresh_flow_run_frequency_seconds);
ABSL_DECLARE_FLAG(
    std::optional<privacy_sandbox::server_common::telemetry::TelemetryFlag>,
    telemetry_config);
ABSL_DECLARE_FLAG(std::optional<std::string>, roma_timeout_ms);
ABSL_DECLARE_FLAG(std::optional<std::string>, collector_endpoint);
ABSL_DECLARE_FLAG(std::optional<std::string>, consented_debug_token);
ABSL_DECLARE_FLAG(std::optional<bool>, enable_otel_based_logging);
ABSL_DECLARE_FLAG(std::optional<bool>, enable_protected_app_signals);
ABSL_DECLARE_FLAG(std::optional<bool>, enable_protected_audience);
ABSL_DECLARE_FLAG(std::optional<int>, ps_verbosity);
ABSL_DECLARE_FLAG(std::optional<int>, max_allowed_size_debug_url_bytes);
ABSL_DECLARE_FLAG(std::optional<int>, max_allowed_size_all_debug_urls_kb);
ABSL_DECLARE_FLAG(std::optional<bool>, enable_chaffing);
ABSL_DECLARE_FLAG(std::optional<int>, debug_sample_rate_micro);
ABSL_DECLARE_FLAG(std::optional<bool>, enable_tkv_v2_browser);
ABSL_DECLARE_FLAG(std::optional<bool>, tkv_egress_tls);
ABSL_DECLARE_FLAG(std::optional<bool>, enable_priority_vector);
ABSL_DECLARE_FLAG(std::optional<bool>, consent_all_requests);
ABSL_DECLARE_FLAG(std::optional<std::string>, seller_code_fetch_config);
ABSL_DECLARE_FLAG(std::optional<int>, curlmopt_maxconnects);
ABSL_DECLARE_FLAG(std::optional<int>, curlmopt_max_total_connections);
ABSL_DECLARE_FLAG(std::optional<int>, curlmopt_max_host_connections);

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char PUBLIC_KEY_ENDPOINT[] = "PUBLIC_KEY_ENDPOINT";
inline constexpr char PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT[] =
    "PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT";
inline constexpr char SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT[] =
    "SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT";
inline constexpr char PRIMARY_COORDINATOR_ACCOUNT_IDENTITY[] =
    "PRIMARY_COORDINATOR_ACCOUNT_IDENTITY";
inline constexpr char SECONDARY_COORDINATOR_ACCOUNT_IDENTITY[] =
    "SECONDARY_COORDINATOR_ACCOUNT_IDENTITY";
inline constexpr char PRIMARY_COORDINATOR_REGION[] =
    "PRIMARY_COORDINATOR_REGION";
inline constexpr char SECONDARY_COORDINATOR_REGION[] =
    "SECONDARY_COORDINATOR_REGION";
inline constexpr char GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER[] =
    "GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER";
inline constexpr char GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER[] =
    "GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER";
inline constexpr char GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL[] =
    "GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL";
inline constexpr char GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL[] =
    "GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL";
inline constexpr char PRIVATE_KEY_CACHE_TTL_SECONDS[] =
    "PRIVATE_KEY_CACHE_TTL_SECONDS";
inline constexpr char KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS[] =
    "KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS";
inline constexpr char TELEMETRY_CONFIG[] = "TELEMETRY_CONFIG";
inline constexpr char TEST_MODE[] = "TEST_MODE";
inline constexpr char HTTPS_FETCH_SKIPS_TLS_VERIFICATION[] =
    "HTTPS_FETCH_SKIPS_TLS_VERIFICATION";
inline constexpr char ROMA_TIMEOUT_MS[] = "ROMA_TIMEOUT_MS";
inline constexpr char COLLECTOR_ENDPOINT[] = "COLLECTOR_ENDPOINT";
inline constexpr char CONSENTED_DEBUG_TOKEN[] = "CONSENTED_DEBUG_TOKEN";
inline constexpr char ENABLE_OTEL_BASED_LOGGING[] = "ENABLE_OTEL_BASED_LOGGING";
inline constexpr char ENABLE_PROTECTED_APP_SIGNALS[] =
    "ENABLE_PROTECTED_APP_SIGNALS";
inline constexpr char ENABLE_PROTECTED_AUDIENCE[] = "ENABLE_PROTECTED_AUDIENCE";
inline constexpr char PS_VERBOSITY[] = "PS_VERBOSITY";
inline constexpr char MAX_ALLOWED_SIZE_DEBUG_URL_BYTES[] =
    "MAX_ALLOWED_SIZE_DEBUG_URL_BYTES";
inline constexpr char MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB[] =
    "MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB";
inline constexpr char ENABLE_CHAFFING[] = "ENABLE_CHAFFING";
inline constexpr char DEBUG_SAMPLE_RATE_MICRO[] = "DEBUG_SAMPLE_RATE_MICRO";
inline constexpr char ENABLE_TKV_V2_BROWSER[] = "ENABLE_TKV_V2_BROWSER";
inline constexpr char TKV_EGRESS_TLS[] = "TKV_EGRESS_TLS";
inline constexpr char ENABLE_PRIORITY_VECTOR[] = "ENABLE_PRIORITY_VECTOR";
inline constexpr char CONSENT_ALL_REQUESTS[] = "CONSENT_ALL_REQUESTS";
inline constexpr absl::string_view SELLER_CODE_FETCH_CONFIG =
    "SELLER_CODE_FETCH_CONFIG";
inline constexpr char CURLMOPT_MAXCONNECTS[] = "CURLMOPT_MAXCONNECTS";
inline constexpr char CURLMOPT_MAX_TOTAL_CONNECTIONS[] =
    "CURLMOPT_MAX_TOTAL_CONNECTIONS";
inline constexpr char CURLMOPT_MAX_HOST_CONNECTIONS[] =
    "CURLMOPT_MAX_HOST_CONNECTIONS";

inline constexpr absl::string_view kCommonServiceFlags[] = {
    PUBLIC_KEY_ENDPOINT,
    PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT,
    SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT,
    PRIMARY_COORDINATOR_ACCOUNT_IDENTITY,
    SECONDARY_COORDINATOR_ACCOUNT_IDENTITY,
    PRIMARY_COORDINATOR_REGION,
    SECONDARY_COORDINATOR_REGION,
    GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER,
    GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER,
    GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL,
    GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL,
    PRIVATE_KEY_CACHE_TTL_SECONDS,
    KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS,
    TEST_MODE,
    HTTPS_FETCH_SKIPS_TLS_VERIFICATION,
    TELEMETRY_CONFIG,
    ROMA_TIMEOUT_MS,
    COLLECTOR_ENDPOINT,
    CONSENTED_DEBUG_TOKEN,
    ENABLE_OTEL_BASED_LOGGING,
    ENABLE_PROTECTED_APP_SIGNALS,
    ENABLE_PROTECTED_AUDIENCE,
    PS_VERBOSITY,
    MAX_ALLOWED_SIZE_DEBUG_URL_BYTES,
    MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB,
    ENABLE_CHAFFING,
    DEBUG_SAMPLE_RATE_MICRO,
    ENABLE_TKV_V2_BROWSER,
    TKV_EGRESS_TLS,
    ENABLE_PRIORITY_VECTOR,
    CONSENT_ALL_REQUESTS,
    CURLMOPT_MAXCONNECTS,
    CURLMOPT_MAX_TOTAL_CONNECTIONS,
    CURLMOPT_MAX_HOST_CONNECTIONS,
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CONSTANTS_COMMON_SERVICE_FLAGS_H_
