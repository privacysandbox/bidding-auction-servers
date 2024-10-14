# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

locals {
  gcp_project_id = "" # Example: "your-gcp-project-123"
  environment    = "" # # Must be <= 3 characters. Example: "abc"
  image_repo     = "" # Example: "us-docker.pkg.dev/your-gcp-project-123/services"

}

provider "google" {
  project = local.gcp_project_id
}

provider "google-beta" {
  project = local.gcp_project_id
}

resource "google_compute_project_metadata" "default" {
  project = local.gcp_project_id
  metadata = {
    enable-oslogin = "FALSE"
  }
}

# See README.md for instructions on how to use the secrets module.
module "secrets" {
  source = "../../../modules/secrets"
}

module "seller" {
  source                = "../../../modules/seller"
  environment           = local.environment
  gcp_project_id        = local.gcp_project_id
  auction_image         = "${local.image_repo}/auction_service:${local.environment}"         # Image built and uploaded by production/packaging/build_and_test_all_in_docker
  seller_frontend_image = "${local.image_repo}/seller_frontend_service:${local.environment}" # Image built and uploaded by production/packaging/build_and_test_all_in_docker

  envoy_port = 51052 # Do not change. Must match production/packaging/gcp/seller_frontend_service/bin/envoy.yaml
  runtime_flags = {
    SELLER_FRONTEND_PORT             = "50051"          # Do not change unless you are modifying the default GCP architecture.
    SELLER_FRONTEND_HEALTHCHECK_PORT = "50050"          # Do not change unless you are modifying the default GCP architecture.
    AUCTION_PORT                     = "50051"          # Do not change unless you are modifying the default GCP architecture.
    AUCTION_SERVER_HOST              = "xds:///auction" # Do not change unless you are modifying the default GCP architecture.
    SFE_INGRESS_TLS                  = "false"          # Do not change unless you are modifying the default GCP architecture.
    BUYER_EGRESS_TLS                 = "true"           # Do not change unless you are modifying the default GCP architecture.
    AUCTION_EGRESS_TLS               = "false"          # Do not change unless you are modifying the default GCP architecture.
    TEST_MODE                        = "false"          # Do not change unless you are testing without key fetching.

    ENABLE_AUCTION_SERVICE_BENCHMARK       = "" # Example: "false"
    GET_BID_RPC_TIMEOUT_MS                 = "" # Example: "60000"
    KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS = "" # Example: "60000"
    SCORE_ADS_RPC_TIMEOUT_MS               = "" # Example: "60000"
    SELLER_ORIGIN_DOMAIN                   = "" # Example: "https://sellerorigin.com"
    KEY_VALUE_SIGNALS_HOST                 = "" # Example: "https://keyvaluesignals.com/trusted-signals"
    BUYER_SERVER_HOSTS                     = "" # Example: "{ \"https://example-bidder.com\": { \"url\": \"dns:///bidding-service-host:443\", \"cloudPlatform\": \"GCP\" } }"
    SELLER_CLOUD_PLATFORMS_MAP             = "" # Example: "{ \"https://partner-seller1.com\": "GCP", \"https://partner-seller2.com\": "AWS"}"
    ENABLE_SELLER_FRONTEND_BENCHMARKING    = "" # Example: "false"
    ENABLE_AUCTION_COMPRESSION             = "" # Example: "false"
    ENABLE_BUYER_COMPRESSION               = "" # Example: "false"
    ENABLE_PROTECTED_APP_SIGNALS           = "" # Example: "false"
    ENABLE_PROTECTED_AUDIENCE              = "" # Example: "true"
    PS_VERBOSITY                           = "" # Example: "10"
    CREATE_NEW_EVENT_ENGINE                = "" # Example: "false"
    SELLER_CODE_FETCH_CONFIG               = "" # Example:
    # "{
    #     "fetchMode": 0,
    #     "auctionJsPath": "",
    #     "auctionJsUrl": "https://example.com/scoreAd.js",
    #     "urlFetchPeriodMs": 13000000,
    #     "urlFetchTimeoutMs": 30000,
    #     "enableSellerDebugUrlGeneration": true,
    #     "enableReportResultUrlGeneration": true,
    #     "enableReportWinUrlGeneration": true,
    #     "enablePrivateAggregateReporting": false,
    #     "buyerReportWinJsUrls": {"https://buyerA_origin.com":"https://buyerA.com/generateBid.js",
    #                              "https://buyerB_origin.com":"https://buyerB.com/generateBid.js",
    #                              "https://buyerC_origin.com":"https://buyerC.com/generateBid.js"},
    #     "protectedAppSignalsBuyerReportWinJsUrls": {"https://buyerA_origin.com":"https://buyerA.com/generateBid.js"}
    #  }"
    UDF_NUM_WORKERS                 = "" # Example: "64" Must be <=vCPUs in auction_machine_type.
    JS_WORKER_QUEUE_LEN             = "" # Example: "200".
    ROMA_TIMEOUT_MS                 = "" # Example: "10000"
    TELEMETRY_CONFIG                = "" # Example: "mode: EXPERIMENT"
    COLLECTOR_ENDPOINT              = "" # Example: "collector-seller-1-${local.environment}.sfe-gcp.com:4317"
    ENABLE_OTEL_BASED_LOGGING       = "" # Example: "false"
    CONSENTED_DEBUG_TOKEN           = "" # Example: "<unique_id>"
    ENABLE_REPORT_WIN_INPUT_NOISING = "" # Example: "true"
    # Coordinator-based attestation flags.
    # These flags are production-ready and you do not need to change them.
    # Reach out to the Privacy Sandbox B&A team to enroll with Coordinators.
    # More information on enrollment can be found here: https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_api.md#enroll-with-coordinators
    PUBLIC_KEY_ENDPOINT                           = "https://publickeyservice.pa.gcp.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys"
    SFE_PUBLIC_KEYS_ENDPOINTS                     = <<EOF
    "{
      "GCP": "https://publickeyservice.pa.gcp.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys",
      "AWS": "https://publickeyservice.pa.aws.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys"
    }"
    EOF
    PUBLIC_KEY_ENDPOINT                           = "https://publickeyservice.pa.gcp.privacysandboxservices.com/.well-known/protected-auction/v1/public-keys"
    PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT      = "https://privatekeyservice-a.pa-3.gcp.privacysandboxservices.com/v1alpha/encryptionKeys"
    SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT    = "https://privatekeyservice-b.pa-4.gcp.privacysandboxservices.com/v1alpha/encryptionKeys"
    PRIMARY_COORDINATOR_ACCOUNT_IDENTITY          = "a-opverifiedusr@ps-pa-coord-prd-g3p-wif.iam.gserviceaccount.com"
    SECONDARY_COORDINATOR_ACCOUNT_IDENTITY        = "b-opverifiedusr@ps-prod-pa-type2-fe82.iam.gserviceaccount.com"
    PRIMARY_COORDINATOR_REGION                    = "us-central1"
    SECONDARY_COORDINATOR_REGION                  = "us-central1"
    GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER   = "projects/732552956908/locations/global/workloadIdentityPools/a-opwip/providers/a-opwip-pvdr"
    GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER = "projects/99438709206/locations/global/workloadIdentityPools/b-opwip/providers/b-opwip-pvdr"
    GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL    = "https://a-us-central1-encryption-key-service-cloudfunctio-j27wiaaz5q-uc.a.run.app"
    GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL  = "https://b-us-central1-encryption-key-service-cloudfunctio-wdqaqbifva-uc.a.run.app"
    PRIVATE_KEY_CACHE_TTL_SECONDS                 = "3974400"
    KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS        = "20000"

    SFE_TLS_KEY                        = module.secrets.tls_key  # You may remove the secrets module and instead either inline or use an auto.tfvars for this variable.
    SFE_TLS_CERT                       = module.secrets.tls_cert # You may remove the secrets module and instead either inline or use an auto.tfvars for this variable.
    MAX_ALLOWED_SIZE_DEBUG_URL_BYTES   = ""                      # Example: "65536"
    MAX_ALLOWED_SIZE_ALL_DEBUG_URLS_KB = ""                      # Example: "3000"

    # TCMalloc related config parameters.
    # See: https://github.com/google/tcmalloc/blob/master/docs/tuning.md
    AUCTION_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND = "4096"
    AUCTION_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES             = "10737418240"
    SFE_TCMALLOC_BACKGROUND_RELEASE_RATE_BYTES_PER_SECOND     = "4096"
    SFE_TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES                 = "10737418240"
  }

  # Please create a Google Cloud domain name, dns zone, and SSL certificate.
  # See demo/project_setup_utils/domain_setup/README.md for more details.
  # If you specify a certificate_map_id, you do not need to specify an ssl_certificate_id.
  frontend_domain_name               = "" # Example: sfe-gcp.com
  frontend_dns_zone                  = "" # Example: "sfe-gcp-com"
  frontend_domain_ssl_certificate_id = "" # Example: "projects/${local.gcp_project_id}/global/sslCertificates/sfe-${local.environment}"
  frontend_certificate_map_id        = "" # Example: "//certificatemanager.googleapis.com/projects/test/locations/global/certificateMaps/wildcard-cert-map"

  operator                           = ""    # Example: "seller-1"
  service_account_email              = ""    # Example: "terraform-sa@{local.gcp_project_id}.iam.gserviceaccount.com"
  vm_startup_delay_seconds           = 200   # Example: 200
  cpu_utilization_percent            = 0.6   # Example: 0.6
  use_confidential_space_debug_image = false # Example: false
  tee_impersonate_service_accounts   = "a-opallowedusr@ps-pa-coord-prd-g3p-svcacc.iam.gserviceaccount.com,b-opallowedusr@ps-prod-pa-type2-fe82.iam.gserviceaccount.com"
  collector_service_port             = 4317
  collector_startup_script = templatefile("../../../services/autoscaling/collector_startup.tftpl", {
    collector_port           = 4317
    otel_collector_image_uri = "otel/opentelemetry-collector-contrib:0.105.0"
    gcs_hmac_key             = module.secrets.gcs_hmac_key
    gcs_hmac_secret          = module.secrets.gcs_hmac_secret
    gcs_bucket               = "" # Example: ${name of a gcs bucket}
    gcs_bucket_prefix        = "" # Example: "consented-eventmessage-${local.environment}"
    file_prefix              = "" # Example: "operator-name"
  })
  region_config = {
    # Example config provided for us-central1 and you may add your own regions.
    "us-central1" = {
      collector = {
        machine_type          = "e2-micro"
        min_replicas          = 1
        max_replicas          = 1
        zones                 = null # Null signifies no zone preference.
        max_rate_per_instance = null # Null signifies no max.
      }
      backend = {
        machine_type          = "n2d-standard-64"
        min_replicas          = 1
        max_replicas          = 5
        zones                 = null # Null signifies no zone preference.
        max_rate_per_instance = null # Null signifies no max.
      }
      frontend = {
        machine_type          = "n2d-standard-64"
        min_replicas          = 1
        max_replicas          = 2
        zones                 = null # Null signifies no zone preference.
        max_rate_per_instance = null # Null signifies no max.
      }
    }
  }
  enable_tee_container_log_redirect = false
}
