/**
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

locals {
  environment = "" # Must be <= 3 characters. Example: "abc"

  buyer_root_domain = "" # Example: "buyer.com"
  buyer_operator    = "" # Example: "buyer1"

  # If you are using a TEE Ad Retrieval KV server, set this to true:
  tee_ad_retrieval_kv_server_exists = false
  ## Then change these values if necessary:
  # Template is a suggestion for likely name if running with mesh.
  # Replace with actual domain, regardless of using mesh or not.
  tee_ad_retrieval_kv_server_domain = "adretrieval-${local.buyer_operator}-${local.environment}-appmesh-virtual-service.${local.buyer_root_domain}"
  # NOTE THAT THE ONLY PORT ALLOWED HERE WHEN RUNNING WITH MESH IS 50051!
  tee_kv_servers_port                = 50051
  tee_ad_retrieval_kv_server_address = "dns:///${local.tee_ad_retrieval_kv_server_domain}:${local.tee_kv_servers_port}"
  # Ditto for these:
  tee_kv_server_exists = false
  # Template is a suggestion for likely name if running with mesh.
  # Replace with actual domain, regardless of using mesh or not.
  tee_kv_server_domain  = "kv-${local.buyer_operator}-${local.environment}-appmesh-virtual-service.${local.buyer_root_domain}"
  tee_kv_server_address = "dns:///${local.tee_kv_server_domain}:${local.tee_kv_servers_port}"

  # Set to true for service mesh, false for Load balancers. MUST be true if using TEE KV or AdRetrieval servers.
  use_service_mesh = true
  # Whether to use TLS-encrypted communication between service mesh envoy sidecars. Defaults to false, as comms take place within a VPC and the critical payload is HPKE-encrypted, and said encryption is terminated inside a TEE.
  use_tls_with_mesh = false

  runtime_flags = {
    BIDDING_PORT                      = "50051" # Do not change unless you are modifying the default AWS architecture.
    BUYER_FRONTEND_PORT               = "50051" # Do not change unless you are modifying the default AWS architecture.
    BFE_INGRESS_TLS                   = "false" # Do not change unless you are modifying the default AWS architecture.
    BIDDING_EGRESS_TLS                = local.use_service_mesh ? "false" : "true"
    AD_RETRIEVAL_KV_SERVER_EGRESS_TLS = local.use_service_mesh ? "false" : "true"
    KV_SERVER_EGRESS_TLS              = local.use_service_mesh ? "false" : "true"
    COLLECTOR_ENDPOINT                = "127.0.0.1:4317" # Do not change unless you are modifying the default AWS architecture.

    ENABLE_BIDDING_SERVICE_BENCHMARK = "" # Example: "false"
    BIDDING_SERVER_ADDR              = local.use_service_mesh ? "dns:///bidding-${local.buyer_operator}-${local.environment}-appmesh-virtual-service.${local.buyer_root_domain}:50051" : "dns:///bidding-${local.environment}.${local.buyer_root_domain}:443"
    GRPC_ARG_DEFAULT_AUTHORITY       = local.use_service_mesh ? "bidding-${local.buyer_operator}-${local.environment}-appmesh-virtual-service.${local.buyer_root_domain}" : "PLACEHOLDER" # "PLACEHOLDER" is a special value that will be ignored by B&A servers. Leave it unchanged if running with Load Balancers.
    # Refers to BYOS Buyer Key-Value Server only.
    BUYER_KV_SERVER_ADDR                                  = ""            # Example: "https://kvserver.com/trusted-signals"
    BUYER_TKV_V2_SERVER_ADDR                              = "PLACEHOLDER" # Example: "dns:///kvserver:443"
    ENABLE_TKV_V2_BROWSER                                 = ""            # Example: "false"
    TKV_EGRESS_TLS                                        = ""            # Example: "false"
    TEE_AD_RETRIEVAL_KV_SERVER_ADDR                       = "${local.tee_ad_retrieval_kv_server_address}"
    TEE_AD_RETRIEVAL_KV_SERVER_GRPC_ARG_DEFAULT_AUTHORITY = local.use_service_mesh ? "${local.tee_ad_retrieval_kv_server_domain}" : "PLACEHOLDER"
    TEE_KV_SERVER_ADDR                                    = "${local.tee_kv_server_address}"
    TEE_KV_SERVER_GRPC_ARG_DEFAULT_AUTHORITY              = local.use_service_mesh ? "${local.tee_kv_server_domain}" : "PLACEHOLDER"
    AD_RETRIEVAL_TIMEOUT_MS                               = "60000"
    GENERATE_BID_TIMEOUT_MS                               = "" # Example: "60000"
    BIDDING_SIGNALS_LOAD_TIMEOUT_MS                       = "" # Example: "60000"
    ENABLE_BUYER_FRONTEND_BENCHMARKING                    = "" # Example: "false"
    CREATE_NEW_EVENT_ENGINE                               = "" # Example: "false"
    ENABLE_BIDDING_COMPRESSION                            = "" # Example: "true"
    TELEMETRY_CONFIG                                      = "" # Example: "mode: EXPERIMENT"
    ENABLE_OTEL_BASED_LOGGING                             = "" # Example: "true"
    CONSENTED_DEBUG_TOKEN                                 = "" # Example: "123456". Consented debugging requests increase server load in production. A high QPS of these requests can lead to unhealthy servers.
    DEBUG_SAMPLE_RATE_MICRO                               = "0"
    TEST_MODE                                             = "" # Example: "false"
    BUYER_CODE_FETCH_CONFIG                               = "" # Example:

    # [BEGIN] Protected App Signals (PAS) related params
    # Refer to: https://github.com/privacysandbox/protected-auction-services-docs/blob/main/bidding_auction_services_protected_app_signals.md
    ENABLE_PROTECTED_APP_SIGNALS                  = "" # Example: "false"
    PROTECTED_APP_SIGNALS_GENERATE_BID_TIMEOUT_MS = "" # Example: "60000"
    EGRESS_SCHEMA_FETCH_CONFIG                    = "" # Example:
    # "{
    #   "fetchMode": 0,
    #   "egressSchemaUrl": "https://example.com/egressSchema.json",
    #   "urlFetchPeriodMs": 130000,
    #   "urlFetchTimeoutMs": 30000
    # }"
    # [END] Protected App Signals (PAS) related params

    ENABLE_PROTECTED_AUDIENCE = "" # Example: "true"
    PS_VERBOSITY              = "" # Example: "10"
    # "{
    #    "fetchMode": 0,
    #    "biddingJsPath": "",
    #    "biddingJsUrl": "https://example.com/generateBid.js",
    #    "protectedAppSignalsBiddingJsUrl": "placeholder",
    #    "biddingWasmHelperUrl": "",
    #    "protectedAppSignalsBiddingWasmHelperUrl": "",
    #    "urlFetchPeriodMs": 13000000,
    #    "urlFetchTimeoutMs": 30000,
    #    "enableBuyerDebugUrlGeneration": true,
    #    "prepareDataForAdsRetrievalJsUrl": "",
    #    "prepareDataForAdsRetrievalWasmHelperUrl": "",
    #    "enablePrivateAggregateReporting": false,
    #  }"
    ROMA_TIMEOUT_MS = "" # Example: "10000"
    # This flag should only be set if console.logs from the AdTech code(Ex:generateBid()) execution need to be exported as VLOG.
    # Note: turning on this flag will lead to higher memory consumption for AdTech code execution
    # and additional latency for parsing the logs.

    # Coordinator-based attestation flags.
    # These flags are production-ready and you do not need to change them.
    PUBLIC_KEY_ENDPOINT                        = "https://publickeyservice.pa.aws.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys"
    PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT   = "https://privatekeyservice-a.pa-3.aws.privacysandboxservices.com/v1alpha/encryptionKeys"
    SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT = "https://privatekeyservice-b.pa-4.aws.privacysandboxservices.com/v1alpha/encryptionKeys"
    PRIMARY_COORDINATOR_REGION                 = "us-east-1"
    SECONDARY_COORDINATOR_REGION               = "us-east-1"
    PRIVATE_KEY_CACHE_TTL_SECONDS              = "3974400"
    KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS     = "20000"
    # Reach out to the Privacy Sandbox B&A team to enroll with Coordinators and update the following flag values.
    # More information on enrollment can be found here: https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_api.md#enroll-with-coordinators
    # Coordinator-based attestation flags:
    PRIMARY_COORDINATOR_ACCOUNT_IDENTITY   = "" # Example: "arn:aws:iam::811625435250:role/a_<YOUR AWS ACCOUNT ID>_coordinator_assume_role"
    SECONDARY_COORDINATOR_ACCOUNT_IDENTITY = "" # Example: "arn:aws:iam::891377198286:role/b_<YOUR AWS ACCOUNT ID>_coordinator_assume_role"

    MAX_ALLOWED_SIZE_DEBUG_URL_BYTES   = "" # Example: "65536"
    MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB = "" # Example: "3000"

    INFERENCE_SIDECAR_BINARY_PATH    = "" # Example: "/server/bin/inference_sidecar_<module_name>"
    INFERENCE_MODEL_BUCKET_NAME      = "" # Example: "<bucket_name>"
    INFERENCE_MODEL_CONFIG_PATH      = "" # Example: "model_config.json"
    INFERENCE_MODEL_FETCH_PERIOD_MS  = "" # Example: "300000"
    INFERENCE_SIDECAR_RUNTIME_CONFIG = "" # Example:
    # "{
    #    "num_interop_threads": 4,
    #    "num_intraop_threads": 4,
    #    "module_name": "tensorflow_v2_14_0",
    #    "cpuset": [0, 1, 2, 3],
    #    "tcmalloc_release_bytes_per_sec": 0,
    #    "tcmalloc_max_total_thread_cache_bytes": 0,
    #    "tcmalloc_max_per_cpu_cache_bytes": 0,
    # }"

    # TCMalloc related config parameters.
    # See: https://github.com/google/tcmalloc/blob/master/docs/tuning.md
    BIDDING_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND = "4096"
    BIDDING_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES             = "10737418240"
    BFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND     = "4096"
    BFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES                 = "10737418240"

    ENABLE_CHAFFING        = "false"
    ENABLE_PRIORITY_VECTOR = "false"
    # Possible values:
    # NOT_FETCHED: No call to KV server is made. All interest groups are sent to generateBid().
    # FETCHED_BUT_OPTIONAL: Call to KV server is made and must not fail. All interest groups are sent to generateBid() irrespective of whether they have bidding signals or not.
    # Any other value/REQUIRED (default): Call to KV server is made and must not fail. Only those interest groups are sent to generateBid() that have at least one bidding signals key for which non-empty bidding signals are fetched.
    BIDDING_SIGNALS_FETCH_MODE = "REQUIRED"

    // The following options can be adjusted to affect the behavior of libcurl, which is used in the BYOS buyer KV signals fetch. Limits in these parameters have been shown to improve BFE performance and stability when under high throughput load. You may adjust these to be more restrictive if you are experiencing BFE becoming unresponsive, or less restrictive if you find BFE throuhgput insufficient.
    // Constrains the size of the libcurl connection cache. Recommended value of 512 for stability. See https://curl.se/libcurl/c/CURLMOPT_MAXCONNECTS.html.
    CURLMOPT_MAXCONNECTS = 512
    // Sets the maximum number of simultaneously open connections. Recommended value of 24 for stability. See https://curl.se/libcurl/c/CURLMOPT_MAX_TOTAL_CONNECTIONS.html.
    CURLMOPT_MAX_TOTAL_CONNECTIONS = 24
    // Sets the maximum number of connections to a single host. 0 is default, means unlimited. https://curl.se/libcurl/c/CURLMOPT_MAX_HOST_CONNECTIONS.html.
    CURLMOPT_MAX_HOST_CONNECTIONS = 0
  }
}

provider "aws" {
  region = "us-east-1"
  alias  = "us-east-1"
}
provider "aws" {
  region = "us-west-1"
  alias  = "us-west-1"
}

module "buyer-us-east-1" {
  # --- Params in upper section are expected to change between regions. ---

  providers = {
    aws = aws.us-east-1
  }
  region          = "us-east-1"
  certificate_arn = "" # Example: "arn:aws:acm:us-west-1:57473821111:certificate/59ebdcbe-2475-4b70-9079-7a360f5c1111"

  # AMIs
  bfe_instance_ami_id     = "" # Example: "ami-0ea7735ce85ec9cf5"
  bidding_instance_ami_id = "" # Example: "ami-0f2f28fc0914f6575"

  # Machine sizing
  bfe_instance_type          = ""    # Example: "c6i.2xlarge"
  bidding_instance_type      = ""    # Example: "c6i.2xlarge"
  bfe_enclave_cpu_count      = 6     # Example: 6
  bfe_enclave_memory_mib     = 12000 # Example: 12000
  bidding_enclave_cpu_count  = 6     # Example: 6
  bidding_enclave_memory_mib = 12000 # Example: 12000
  runtime_flags = merge(local.runtime_flags, {
    UDF_NUM_WORKERS     = "" # Example: "48" Must be <=vCPUs in bidding_enclave_cpu_count, and should be equal for best performance.
    JS_WORKER_QUEUE_LEN = "" # Example: "100".
  })

  # Autoscaling
  bfe_autoscaling_desired_capacity     = 3 # Example: 3
  bfe_autoscaling_max_size             = 5 # Example: 5
  bfe_autoscaling_min_size             = 1 # Example: 1
  bidding_autoscaling_desired_capacity = 3 # Example: 3
  bidding_autoscaling_max_size         = 5 # Example: 5
  bidding_autoscaling_min_size         = 1 # Example: 1


  # --- Params below are not generally expected to change between regions. ---

  source                = "../../../modules/buyer"
  environment           = local.environment
  enclave_debug_mode    = false # Example: false, set to true for extended logs
  root_domain           = local.buyer_root_domain
  root_domain_zone_id   = "" # Example: "Z1011487GET92S4MN4CM"
  operator              = local.buyer_operator
  coordinator_role_arns = [var.runtime_flags.PRIMARY_COORDINATOR_ACCOUNT_IDENTITY, var.runtime_flags.SECONDARY_COORDINATOR_ACCOUNT_IDENTITY]

  # Certificate authority
  country_for_cert_auth      = "" # Example: "US"
  business_org_for_cert_auth = "" # Example: "Privacy Sandbox"
  state_for_cert_auth        = "" # Example: "California"
  org_unit_for_cert_auth     = "" # Example: "Bidding and Auction Servers"
  locality_for_cert_auth     = "" # Example: "Mountain View"

  # Trusted Key-Value Server Parameters; required for PAS.
  kv_server_virtual_service_name              = local.tee_kv_server_exists ? "${local.tee_kv_server_domain}" : "PLACEHOLDER"
  ad_retrieval_kv_server_virtual_service_name = local.tee_ad_retrieval_kv_server_exists ? "${local.tee_ad_retrieval_kv_server_domain}" : "PLACEHOLDER"
  # NOTE THAT THE ONLY PORT ALLOWED HERE WHEN RUNNING WITH MESH IS 50051!
  tee_kv_servers_port = local.tee_kv_servers_port

  # Service Mesh (deprecated by AWS)
  use_service_mesh  = local.use_service_mesh
  use_tls_with_mesh = local.use_tls_with_mesh

  # The value is required for "awss3" exporter defined in production/packaging/aws/common/ami/otel_collector_config.yaml.
  # Alternatively, "awss3" must not be used in otel_collector_config.yaml.
  consented_request_s3_bucket = "" # Example: ${name of a s3 bucket}.
}

module "buyer-us-west-1" {
  # --- Params in upper section are expected to change between regions. ---

  providers = {
    aws = aws.us-west-1
  }
  region          = "us-west-1"
  certificate_arn = "" # Example: "arn:aws:acm:us-west-1:57473821111:certificate/59ebdcbe-2475-4b70-9079-7a360f5c1111"

  # AMIs
  bfe_instance_ami_id     = "" # Example: "ami-0ea7735ce85ec9cf5"
  bidding_instance_ami_id = "" # Example: "ami-0f2f28fc0914f6575"

  # Machine sizing
  bfe_instance_type          = ""    # Example: "c6i.2xlarge"
  bidding_instance_type      = ""    # Example: "c6i.2xlarge"
  bfe_enclave_cpu_count      = 6     # Example: 6
  bfe_enclave_memory_mib     = 12000 # Example: 12000
  bidding_enclave_cpu_count  = 6     # Example: 6
  bidding_enclave_memory_mib = 12000 # Example: 12000
  runtime_flags = merge(local.runtime_flags, {
    UDF_NUM_WORKERS     = "" # Example: "48" Must be <=vCPUs in bidding_enclave_cpu_count, and should be equal for best performance.
    JS_WORKER_QUEUE_LEN = "" # Example: "100".
  })

  # Autoscaling
  bfe_autoscaling_desired_capacity     = 3 # Example: 3
  bfe_autoscaling_max_size             = 5 # Example: 5
  bfe_autoscaling_min_size             = 1 # Example: 1
  bidding_autoscaling_desired_capacity = 3 # Example: 3
  bidding_autoscaling_max_size         = 5 # Example: 5
  bidding_autoscaling_min_size         = 1 # Example: 1


  # --- Params below are not generally expected to change between regions. ---

  source                = "../../../modules/buyer"
  environment           = local.environment
  enclave_debug_mode    = false # Example: false, set to true for extended logs
  root_domain           = local.buyer_root_domain
  root_domain_zone_id   = "" # Example: "Z1011487GET92S4MN4CM"
  operator              = local.buyer_operator
  coordinator_role_arns = [var.runtime_flags.PRIMARY_COORDINATOR_ACCOUNT_IDENTITY, var.runtime_flags.SECONDARY_COORDINATOR_ACCOUNT_IDENTITY]

  # Certificate authority
  country_for_cert_auth      = "" # Example: "US"
  business_org_for_cert_auth = "" # Example: "Privacy Sandbox"
  state_for_cert_auth        = "" # Example: "California"
  org_unit_for_cert_auth     = "" # Example: "Bidding and Auction Servers"
  locality_for_cert_auth     = "" # Example: "Mountain View"

  # Trusted Key-Value Server Parameters; required for PAS.
  kv_server_virtual_service_name              = local.tee_kv_server_exists ? "${local.tee_kv_server_domain}" : "PLACEHOLDER"
  ad_retrieval_kv_server_virtual_service_name = local.tee_ad_retrieval_kv_server_exists ? "${local.tee_ad_retrieval_kv_server_domain}" : "PLACEHOLDER"
  # NOTE THAT THE ONLY PORT ALLOWED HERE WHEN RUNNING WITH MESH IS 50051!
  tee_kv_servers_port = local.tee_kv_servers_port

  # Service Mesh (deprecated by AWS)
  use_service_mesh  = local.use_service_mesh
  use_tls_with_mesh = local.use_tls_with_mesh

  # The value is required for "awss3" exporter defined in production/packaging/aws/common/ami/otel_collector_config.yaml.
  # Alternatively, "awss3" must not be used in otel_collector_config.yaml.
  consented_request_s3_bucket = "" # Example: ${name of a s3 bucket}.
}
