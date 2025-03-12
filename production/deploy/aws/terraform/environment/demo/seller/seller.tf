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

  seller_root_domain = "" # Example: "seller.com"
  seller_operator    = "" # Example: "seller1"

  # Set to true for service mesh, false for Load balancers
  use_service_mesh = true
  # Whether to use TLS-encrypted communication between service mesh envoy sidecars. Defaults to false, as comms take place within a VPC and the critical payload is HPKE-encrypted, and said encryption is terminated inside a TEE.
  use_tls_with_mesh = false

  runtime_flags = {
    AUCTION_PORT         = "50051"                                   # Do not change unless you are modifying the default AWS architecture.
    SELLER_FRONTEND_PORT = "50051"                                   # Do not change unless you are modifying the default AWS architecture.
    SFE_INGRESS_TLS      = "false"                                   # Do not change unless you are modifying the default AWS architecture.
    BUYER_EGRESS_TLS     = "true"                                    # Do not change unless you are modifying the default AWS architecture.
    AUCTION_EGRESS_TLS   = local.use_service_mesh ? "false" : "true" # Do not change unless you are modifying the default AWS architecture.
    COLLECTOR_ENDPOINT   = "127.0.0.1:4317"                          # Do not change unless you are modifying the default AWS architecture.

    ENABLE_AUCTION_SERVICE_BENCHMARK = "" # Example: "false"
    GET_BID_RPC_TIMEOUT_MS           = "" # Example: "60000"
    SCORE_ADS_RPC_TIMEOUT_MS         = "" # Example: "60000"
    SELLER_ORIGIN_DOMAIN             = "" # Example: "https://sellerorigin.com"
    AUCTION_SERVER_HOST              = local.use_service_mesh ? "dns:///auction-${local.seller_operator}-${local.environment}-appmesh-virtual-service.${local.seller_root_domain}:50051" : "dns:///auction-${local.environment}.${local.seller_root_domain}:443"
    GRPC_ARG_DEFAULT_AUTHORITY       = local.use_service_mesh ? "auction-${local.seller_operator}-${local.environment}-appmesh-virtual-service.${local.seller_root_domain}" : "PLACEHOLDER" # "PLACEHOLDER" is a special value that will be ignored by B&A servers. Leave it unchanged if running with Load Balancers.
    # Refers to BYOS Seller Key-Value Server only.
    K_ANON_API_KEY = "PLACEHOLDER" # API Key used to query k-anon service.

    # [BEGIN] Trusted KV real time signal fetching params
    ENABLE_TKV_V2_BROWSER                  = ""            # Example: "false", Whether or not to use a trusted KV for browser clients. (Android clients traffic always need a trusted KV.)
    TKV_EGRESS_TLS                         = ""            # Example: "false", Whether or not to use TLS when talking to TKV. (Useful when TKV is not in the same VPC/mesh)
    TRUSTED_KEY_VALUE_V2_SIGNALS_HOST      = "PLACEHOLDER" # Example: "dns:///keyvaluesignals:443", Address where the TKV is listening.
    KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS = ""            # Example: "60000"
    # [END] Trusted KV real time signal fetching params

    # [BEGIN] Untrusted KV real time signal fetching params
    KEY_VALUE_SIGNALS_HOST = "" # Example: "https://keyvaluesignals.com/trusted-signals", Address of the BYOS KV service (Only supported for Web traffic)
    # [END] Untrusted KV real time signal fetching params

    BUYER_SERVER_HOSTS                  = "" # Example: "{ \"https://bid1.com\": { \"url\": \"dns:///bidding1.com:443\", \"cloudPlatform\": \"AWS\" } }"
    SELLER_CLOUD_PLATFORMS_MAP          = "" # Example: "{ \"https://partner-seller1.com\": "GCP", \"https://partner-seller2.com\": "AWS"}"
    ENABLE_SELLER_FRONTEND_BENCHMARKING = "" # Example: "false"
    ENABLE_AUCTION_COMPRESSION          = "" # Example: "false"
    ENABLE_BUYER_COMPRESSION            = "" # Example: "false"
    ENABLE_PROTECTED_APP_SIGNALS        = "" # Example: "false"
    ENABLE_PROTECTED_AUDIENCE           = "" # Example: "true"
    PS_VERBOSITY                        = "" # Example: "10"
    CREATE_NEW_EVENT_ENGINE             = "" # Example: "false"
    TELEMETRY_CONFIG                    = "" # Example: "mode: EXPERIMENT"
    ENABLE_OTEL_BASED_LOGGING           = "" # Example: "true"
    CONSENTED_DEBUG_TOKEN               = "" # Example: "123456". Consented debugging requests increase server load in production. A high QPS of these requests can lead to unhealthy servers.
    DEBUG_SAMPLE_RATE_MICRO             = "0"
    TEST_MODE                           = "" # Example: "false"
    SELLER_CODE_FETCH_CONFIG            = "" # Example:
    # "{
    #     "fetchMode": 0,
    #     "auctionJsPath": "",
    #     "auctionJsUrl": "https://example.com/scoreAd.js",
    #     "urlFetchPeriodMs": 13000000,
    #     "urlFetchTimeoutMs": 30000,
    #     "enableSellerDebugUrlGeneration": true,
    # This flag should only be set if console.logs from the AdTech code(Ex:scoreAd(), reportResult(), reportWin())
    # execution need to be exported as VLOG.
    # Note: turning on this flag will lead to higher memory consumption for AdTech code execution
    # and additional latency for parsing the logs.
    #     "enableReportResultUrlGeneration": true,
    #     "enableReportWinUrlGeneration": true,
    #     "enablePrivateAggregateReporting": false,
    #     "buyerReportWinJsUrls": {"https://buyerA_origin.com":"https://buyerA.com/generateBid.js",
    #                              "https://buyerB_origin.com":"https://buyerB.com/generateBid.js",
    #                              "https://buyerC_origin.com":"https://buyerC.com/generateBid.js"},
    #     "protectedAppSignalsBuyerReportWinJsUrls": {"https://buyerA_origin.com":"https://buyerA.com/generateBid.js"}

    #  }"
    ROMA_TIMEOUT_MS                     = "" # Example: "10000"
    ENABLE_REPORT_WIN_INPUT_NOISING     = "" # Example: "true"
    K_ANON_TOTAL_NUM_HASH               = "" # Example: "1000"
    EXPECTED_K_ANON_TO_NON_K_ANON_RATIO = "" # Example: "1.0"
    K_ANON_CLIENT_TIME_OUT_MS           = "" # Example: "60000"
    NUM_K_ANON_SHARDS                   = "" # Example: "1"
    NUM_NON_K_ANON_SHARDS               = "" # Example: "1"
    ENABLE_K_ANON_QUERY_CACHE           = "" # Example: "true"

    # Coordinator-based attestation flags.
    # These flags are production-ready and you do not need to change them.
    SFE_PUBLIC_KEYS_ENDPOINTS                  = <<EOF
    {
      "GCP": "https://publickeyservice.pa.gcp.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys",
      "AWS": "https://publickeyservice.pa.aws.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys"
    }
    EOF
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

    # TCMalloc related config parameters.
    # See: https://github.com/google/tcmalloc/blob/master/docs/tuning.md
    AUCTION_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND = "4096"
    AUCTION_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES             = "10737418240"
    SFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND     = "4096"
    SFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES                 = "10737418240"

    ENABLE_CHAFFING        = "" # Example: "false"
    ENABLE_PRIORITY_VECTOR = "" # Example: "true"
    # Possible values:
    # NOT_FETCHED: No call to KV server is made. All ads are sent to scoreAd().
    # FETCHED_BUT_OPTIONAL: Call to KV server is made and must not fail. All ads are sent to scoreAd() irrespective of whether their adRenderUrls have scoring signals or not.
    # Any other value/REQUIRED (default): Call to KV server is made and must not fail. Only those ads are sent to scoreAd() that have scoring signals for their adRenderUrl.
    SCORING_SIGNALS_FETCH_MODE      = "REQUIRED"
    ALLOW_COMPRESSED_AUCTION_CONFIG = "" # Example: "true"
    ENABLE_BUYER_CACHING            = "" # Example: "true"
  }
}
provider "aws" {
  region = "us-east-1"
  alias  = "us-east-1"
}


module "seller-us-east-1" {
  # --- Params in upper section are expected to change between regions. ---

  providers = {
    aws = aws.us-east-1
  }
  region          = "us-east-1"
  certificate_arn = "" # Example: "arn:aws:acm:us-west-1:574738241422:certificate/b8b0a33f-0821-1111-8464-bc8da130a7fe"

  # AMIs
  sfe_instance_ami_id     = "" # Example: "ami-0ff8ad2fa8512a078"
  auction_instance_ami_id = "" # Example: "ami-0ea85f493f16aba3c"

  # Machine sizing
  sfe_instance_type          = ""    # Example: "c6i.2xlarge"
  auction_instance_type      = ""    # Example: "c6i.2xlarge"
  sfe_enclave_cpu_count      = 6     # Example: 6
  sfe_enclave_memory_mib     = 12000 # Example: 12000
  auction_enclave_cpu_count  = 6     # Example: 6
  auction_enclave_memory_mib = 12000 # Example: 12000
  runtime_flags = merge(local.runtime_flags, {
    UDF_NUM_WORKERS     = "" # Example: "48" Must be <=vCPUs in auction_enclave_cpu_count, and should be equal for best performance.
    JS_WORKER_QUEUE_LEN = "" # Example: "100".
  })

  # Autoscaling
  sfe_autoscaling_desired_capacity     = 3 # Example: 3
  sfe_autoscaling_max_size             = 5 # Example: 5
  sfe_autoscaling_min_size             = 1 # Example: 1
  auction_autoscaling_desired_capacity = 3 # Example: 3
  auction_autoscaling_max_size         = 5 # Example: 5
  auction_autoscaling_min_size         = 1 # Example: 1


  # --- Params below are not generally expected to change between regions. ---

  source                = "../../../modules/seller"
  environment           = local.environment
  enclave_debug_mode    = false # Example: false, set to true for extended logs
  root_domain           = local.seller_root_domain
  root_domain_zone_id   = "" # Example: "Z010286721PKVM00UYU50"
  operator              = local.seller_operator
  coordinator_role_arns = [var.runtime_flags.PRIMARY_COORDINATOR_ACCOUNT_IDENTITY, var.runtime_flags.SECONDARY_COORDINATOR_ACCOUNT_IDENTITY]

  # Certificate authority
  country_for_cert_auth      = "" # Example: "US"
  business_org_for_cert_auth = "" # Example: "Privacy Sandbox"
  state_for_cert_auth        = "" # Example: "California"
  org_unit_for_cert_auth     = "" # Example: "Bidding and Auction Servers"
  locality_for_cert_auth     = "" # Example: "Mountain View"

  # NOTE THAT THE ONLY PORT ALLOWED HERE WHEN RUNNING WITH MESH IS 50051!
  tee_kv_servers_port = 50051 # Example: 50051

  # Service Mesh (deprecated by AWS)
  use_service_mesh  = local.use_service_mesh
  use_tls_with_mesh = local.use_tls_with_mesh

  # The value is required for "awss3" exporter defined in production/packaging/aws/common/ami/otel_collector_config.yaml.
  # Alternatively, "awss3" must not be used in otel_collector_config.yaml.
  consented_request_s3_bucket = "" # Example: ${name of a s3 bucket}
}
