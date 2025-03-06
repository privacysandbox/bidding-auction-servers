// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/constants/common_service_flags.h"

#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(std::optional<std::string>, public_key_endpoint, std::nullopt,
          "Endpoint serving set of public keys used for encryption");
ABSL_FLAG(std::optional<std::string>, primary_coordinator_private_key_endpoint,
          std::nullopt,
          "Primary coordinator's private key vending service endpoint");
ABSL_FLAG(std::optional<std::string>,
          secondary_coordinator_private_key_endpoint, std::nullopt,
          "Secondary coordinator's Private Key Vending Service endpoint");
ABSL_FLAG(std::optional<std::string>, primary_coordinator_account_identity,
          std::nullopt,
          "The identity used to communicate with the primary coordinator's "
          "Private Key Vending Service");
ABSL_FLAG(std::optional<std::string>, secondary_coordinator_account_identity,
          std::nullopt,
          "The identity used to communicate with the secondary coordinator's "
          "Private Key Vending Service");
ABSL_FLAG(
    std::optional<std::string>, primary_coordinator_region, std::nullopt,
    "The region of the primary coordinator's Private Key Vending Service");
ABSL_FLAG(
    std::optional<std::string>, secondary_coordinator_region, std::nullopt,
    "The region of the secondary coordinator's Private Key Vending Service");
ABSL_FLAG(std::optional<std::string>,
          gcp_primary_workload_identity_pool_provider, std::nullopt,
          "The GCP primary workload identity pool provider resource name.");
ABSL_FLAG(std::optional<std::string>,
          gcp_secondary_workload_identity_pool_provider, std::nullopt,
          "The GCP secondary workload identity pool provider resource name.");

ABSL_FLAG(std::optional<std::string>,
          gcp_primary_key_service_cloud_function_url, std::nullopt,
          "GCP primary private key vending service cloud function URL.");
ABSL_FLAG(std::optional<std::string>,
          gcp_secondary_key_service_cloud_function_url, std::nullopt,
          "GCP secondary private key vending service cloud function URL.");
ABSL_FLAG(std::optional<int>, private_key_cache_ttl_seconds,
          3888000,  // 45 days
          "The duration of how long encryption keys are cached in memory");
ABSL_FLAG(std::optional<int>, key_refresh_flow_run_frequency_seconds,
          10800,  // 3 hours
          "The frequency at which the encryption key refresh flow should run.");
// Master flag for controlling all the features that needs to turned on for
// testing.
ABSL_FLAG(std::optional<bool>, test_mode, std::nullopt, "Enable test mode");
ABSL_FLAG(std::optional<bool>, https_fetch_skips_tls_verification, std::nullopt,
          "Whether we skip ALL tls verification on HTTPS fetch requests from "
          "libcurl. Only recommended for local testing.");
ABSL_FLAG(
    std::optional<privacy_sandbox::server_common::telemetry::TelemetryFlag>,
    telemetry_config, std::nullopt, "configure telemetry.");
ABSL_FLAG(std::optional<std::string>, roma_timeout_ms, std::nullopt,
          "The timeout used by Roma for dispatch requests");
ABSL_FLAG(std::optional<std::string>, collector_endpoint, std::nullopt,
          "The endpoint of the OpenTelemetry Collector");
ABSL_FLAG(std::optional<std::string>, consented_debug_token, std::nullopt,
          "The secret token for AdTech consented debugging. The server will "
          "enable logs only for the request with the matching token. It should "
          "not be publicly shared.");
ABSL_FLAG(std::optional<bool>, enable_otel_based_logging, false,
          "Whether the OpenTelemetry Logs is enabled. It's used for consented "
          "debugging.");
ABSL_FLAG(std::optional<bool>, enable_protected_app_signals, false,
          "Enables the protected app signals support.");
ABSL_FLAG(std::optional<bool>, enable_protected_audience, true,
          "Enables the protected audience support.");
ABSL_FLAG(std::optional<int>, ps_verbosity, std::nullopt,
          "verbosity of consented debug logger in prod; or verbosity of all "
          "loggers in non_prod");
ABSL_FLAG(std::optional<int>, max_allowed_size_debug_url_bytes, 1,
          "Max allowed size of a debug win or loss URL in bytes");
ABSL_FLAG(std::optional<int>, max_allowed_size_all_debug_urls_kb, 1,
          "Max allowed size of all debug win or loss URLs summed together in "
          "kilobytes");
ABSL_FLAG(std::optional<bool>, enable_chaffing, false,
          "If true, chaff requests are sent out from the SFE. Chaff requests "
          "are requests sent to buyers not participating in an auction to mask "
          "the buyers associated with a client request.");
ABSL_FLAG(std::optional<int>, debug_sample_rate_micro, 0,
          "Set a value between 0 (never debug) and 1,000,000 (always debug) to "
          "determine the sampling rate of requests eligible for debugging. "
          "Chosen requests export"
          "privacy_sandbox.bidding_auction_servers.EventMessage");
ABSL_FLAG(std::optional<bool>, enable_tkv_v2_browser, false,
          "If true, TKV requests are sent in v2 format for "
          "CLIENT_TYPE_BROWSER. Historically, they were "
          "sent in a simpler v1 protocol. B&A needs to support both for some "
          "time. Eventually, only v2 should be supported and this flag "
          "will be removed");
ABSL_FLAG(std::optional<bool>, tkv_egress_tls, false,
          "If true, TKV service gRPC client uses TLS.");
ABSL_FLAG(std::optional<bool>, enable_priority_vector, false,
          "Enable priority vector to filter out lower priority interest groups "
          "on the buyer side");
ABSL_FLAG(
    std::optional<bool>, consent_all_requests, false,
    "If true, all request will be treated as consented request in non_prod");
ABSL_FLAG(
    std::optional<std::string>, seller_code_fetch_config, std::nullopt,
    "The JSON string for config fields necessary for AdTech code fetching.");
ABSL_FLAG(std::optional<int>, curlmopt_maxconnects, 0L,
          "Limits number of connections left alive in cache.");
ABSL_FLAG(std::optional<int>, curlmopt_max_total_connections, 0L,
          "Limits number of connections allowed active.");
ABSL_FLAG(std::optional<int>, curlmopt_max_host_connections, 0L,
          "The maximum amount of simultaneously open connections libcurl may "
          "hold to a single host.");
