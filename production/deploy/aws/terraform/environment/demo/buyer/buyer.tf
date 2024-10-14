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
  region      = "" # Example: ["us-central1", "us-west1"]
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
}

provider "aws" {
  region = local.region
}

module "buyer" {
  source                                      = "../../../modules/buyer"
  environment                                 = local.environment
  region                                      = local.region
  enclave_debug_mode                          = false # Example: false, set to true for extended logs
  root_domain                                 = local.buyer_root_domain
  root_domain_zone_id                         = "" # Example: "Z1011487GET92S4MN4CM"
  certificate_arn                             = "" # Example: "arn:aws:acm:us-west-1:57473821111:certificate/59ebdcbe-2475-4b70-9079-7a360f5c1111"
  operator                                    = local.buyer_operator
  bfe_instance_ami_id                         = ""    # Example: "ami-0ea7735ce85ec9cf5"
  bidding_instance_ami_id                     = ""    # Example: "ami-0f2f28fc0914f6575"
  bfe_instance_type                           = ""    # Example: "c6i.2xlarge"
  bidding_instance_type                       = ""    # Example: "c6i.2xlarge"
  bfe_enclave_cpu_count                       = 6     # Example: 6
  bfe_enclave_memory_mib                      = 12000 # Example: 12000
  bidding_enclave_cpu_count                   = 6     # Example: 6
  bidding_enclave_memory_mib                  = 12000 # Example: 12000
  bfe_autoscaling_desired_capacity            = 3     # Example: 3
  bfe_autoscaling_max_size                    = 5     # Example: 5
  bfe_autoscaling_min_size                    = 1     # Example: 1
  bidding_autoscaling_desired_capacity        = 3     # Example: 3
  bidding_autoscaling_max_size                = 5     # Example: 5
  bidding_autoscaling_min_size                = 1     # Example: 1
  country_for_cert_auth                       = ""    # Example: "US"
  business_org_for_cert_auth                  = ""    # Example: "Privacy Sandbox"
  state_for_cert_auth                         = ""    # Example: "California"
  org_unit_for_cert_auth                      = ""    # Example: "Bidding and Auction Servers"
  locality_for_cert_auth                      = ""    # Example: "Mountain View"
  kv_server_virtual_service_name              = local.tee_kv_server_exists ? "${local.tee_kv_server_domain}" : "PLACEHOLDER"
  ad_retrieval_kv_server_virtual_service_name = local.tee_ad_retrieval_kv_server_exists ? "${local.tee_ad_retrieval_kv_server_domain}" : "PLACEHOLDER"
  use_service_mesh                            = local.use_service_mesh
  use_tls_with_mesh                           = local.use_tls_with_mesh
  tee_kv_servers_port                         = local.tee_kv_servers_port

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
    BUYER_KV_SERVER_ADDR                                  = "" # Example: "https://kvserver.com/trusted-signals"
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
    CONSENTED_DEBUG_TOKEN                                 = "" # Example: "123456"
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
    UDF_NUM_WORKERS     = "" # Example: "48" Must be <=vCPUs in bidding_enclave_cpu_count, and should be equal for best performance.
    JS_WORKER_QUEUE_LEN = "" # Example: "100".
    ROMA_TIMEOUT_MS     = "" # Example: "10000"
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

    INFERENCE_SIDECAR_BINARY_PATH = "" # Example: "/server/bin/inference_sidecar_<module_name>"
    INFERENCE_MODEL_BUCKET_NAME   = "" # Example: "<bucket_name>"
    INFERENCE_MODEL_BUCKET_PATHS  = "" # Example: "<model_path1>,<model_path2>"

    # TCMalloc related config parameters.
    # See: https://github.com/google/tcmalloc/blob/master/docs/tuning.md
    BIDDING_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND = "4096"
    BIDDING_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES             = "10737418240"
    BFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND     = "4096"
    BFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES                 = "10737418240"
  }
  consented_request_s3_bucket = "" # Example: ${name of a s3 bucket}
}
