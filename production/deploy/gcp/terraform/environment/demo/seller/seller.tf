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

module "seller" {
  source                = "../../../modules/seller"
  environment           = local.environment
  gcp_project_id        = local.gcp_project_id
  auction_image         = "${local.image_repo}/auction_service:${local.environment}"         # Image built and uploaded by production/packaging/build_and_test_all_in_docker
  seller_frontend_image = "${local.image_repo}/seller_frontend_service:${local.environment}" # Image built and uploaded by production/packaging/build_and_test_all_in_docker

  envoy_port = 51052 # Do not change. Must match production/packaging/gcp/seller_frontend_service/bin/envoy.yaml
  runtime_flags = {
    SELLER_FRONTEND_PORT = "50051"          # Do not change unless you are modifying the default GCP architecture.
    AUCTION_PORT         = "50051"          # Do not change unless you are modifying the default GCP architecture.
    AUCTION_SERVER_HOST  = "xds:///auction" # Do not change unless you are modifying the default GCP architecture.
    SFE_INGRESS_TLS      = "false"          # Do not change unless you are modifying the default GCP architecture.
    BUYER_EGRESS_TLS     = "true"           # Do not change unless you are modifying the default GCP architecture.
    AUCTION_EGRESS_TLS   = "false"          # Do not change unless you are modifying the default GCP architecture.
    ENABLE_ENCRYPTION    = "true"           # Do not change unless you are testing without encryption.
    TEST_MODE            = "false"          # Do not change unless you are testing without key fetching.

    ENABLE_AUCTION_SERVICE_BENCHMARK       = "" # Example: "false"
    GET_BID_RPC_TIMEOUT_MS                 = "" # Example: "60000"
    KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS = "" # Example: "60000"
    SCORE_ADS_RPC_TIMEOUT_MS               = "" # Example: "60000"
    SELLER_ORIGIN_DOMAIN                   = "" # Example: "https://securepubads.g.doubleclick.net"
    KEY_VALUE_SIGNALS_HOST                 = "" # Example: "https://pubads.g.doubleclick.net/td/sts"
    BUYER_SERVER_HOSTS                     = "" # Example: "{\"https://example-bidder.com\": \"dns:///bidding-service-host:443\"}"
    ENABLE_SELLER_FRONTEND_BENCHMARKING    = "" # Example: "false"
    ENABLE_AUCTION_COMPRESSION             = "" # Example: "false"
    ENABLE_BUYER_COMPRESSION               = "" # Example: "false"
    CREATE_NEW_EVENT_ENGINE                = "" # Example: "false"
    SELLER_CODE_FETCH_CONFIG               = "" # Example:
    # "{
    #     "auctionJsPath": "",
    #     "auctionJsUrl": "https://example.com/scoreAd.js",
    #     "urlFetchPeriodMs": 13000000,
    #     "urlFetchTimeoutMs": 30000,
    #     "enableSellerDebugUrlGeneration": true,
    #     "enableAdtechCodeLogging": false,
    #     "enableReportResultUrlGeneration": false,
    #     "enableReportWinUrlGeneration": false,
    #     "buyerReportWinJsUrls": {"https://buyerA_origin.com":"https://buyerA.com/generateBid.js",
    #                              "https://buyerB_origin.com":"https://buyerB.com/generateBid.js",
    #                              "https://buyerC_origin.com":"https://buyerC.com/generateBid.js"}
    #  }"
    JS_NUM_WORKERS      = "" # Example: "64" Must be <=vCPUs in auction_machine_type.
    JS_WORKER_QUEUE_LEN = "" # Example: "200".
    JS_WORKER_MEM_MB    = "" # Example: "3072" JS_WORKER_MEM_MB/JS_WORKER_QUEUE_LEN > average JS request size.
    ROMA_TIMEOUT_MS     = "" # Example: "10000"
    TELEMETRY_CONFIG    = "" # Example: "mode: EXPERIMENT"
    COLLECTOR_ENDPOINT  = "" # Example: "collector-seller-1-${local.environment}.sfe-gcp.com:4317"

    # Reach out to the Privacy Sandbox B&A team to enroll with Coordinators and update the following flag values.
    # More information on enrollment can be found here: https://github.com/privacysandbox/fledge-docs/blob/main/bidding_auction_services_api.md#enroll-with-coordinators
    # Coordinator-based attestation flags:
    PUBLIC_KEY_ENDPOINT                           = "" # Example: "https://publickeyservice-staging-a.gcp.testing.dev/v1alpha/publicKeys"
    PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT      = "" # Example: "https://privatekeyservice-staging-a.gcp.testing.dev/v1alpha/encryptionKeys"
    SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT    = "" # Example: "https://privatekeyservice-staging-b.gcp.testing.dev/v1alpha/encryptionKeys"
    PRIMARY_COORDINATOR_ACCOUNT_IDENTITY          = "" # Example: "staging-a-opverifiedusr@coordinator1.iam.gserviceaccount.com"
    SECONDARY_COORDINATOR_ACCOUNT_IDENTITY        = "" # Example: "staging-b-opverifiedusr@coordinator2.iam.gserviceaccount.com"
    PRIMARY_COORDINATOR_REGION                    = "" # Example: "us-central1"
    SECONDARY_COORDINATOR_REGION                  = "" # Example: "us-central1"
    GCP_PRIMARY_WORKLOAD_IDENTITY_POOL_PROVIDER   = "" # Example: "projects/12345/locations/global/workloadIdentityPools/staging-a-opwip/providers/staging-a-opwip-pvdr"
    GCP_SECONDARY_WORKLOAD_IDENTITY_POOL_PROVIDER = "" # Example: "projects/78910/locations/global/workloadIdentityPools/staging-b-opwip/providers/staging-b-opwip-pvdr"
    GCP_PRIMARY_KEY_SERVICE_CLOUD_FUNCTION_URL    = "" # Example: "https://staging-a-us-central1-encryption-key-service-test-uc.a.run.app"
    GCP_SECONDARY_KEY_SERVICE_CLOUD_FUNCTION_URL  = "" # Example: "https://staging-b-us-central1-encryption-key-service-test-uc.a.run.app"
    PRIVATE_KEY_CACHE_TTL_SECONDS                 = "" # Example:  "3974400" (46 days)
    KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS        = "" # Example: "20000"
    ENABLE_REPORT_RESULT_URL_GENERATION           = "" # Example: "false"
    ENABLE_REPORT_WIN_URL_GENERATION              = "" # Example: "false"

    SFE_TLS_KEY  = "" # You can either set this here or via a secrets.auto.tfvars.
    SFE_TLS_CERT = "" # You can either set this here or via a secrets.auto.tfvars.
  }

  # Please manually create a Google Cloud domain name, dns zone, and SSL certificate.
  frontend_domain_name               = "" # Example: sfe-gcp.com
  frontend_dns_zone                  = "" # Example: "sfe-gcp-com"
  frontend_domain_ssl_certificate_id = "" # Example: "projects/${local.gcp_project_id}/global/sslCertificates/sfe-${local.environment}"

  operator                           = ""    # Example: "seller-1"
  regions                            = []    # Example: ["us-central1", "us-west1"]
  service_account_email              = ""    # Example: "terraform-sa@{local.gcp_project_id}.iam.gserviceaccount.com"
  sfe_machine_type                   = ""    # Example: "n2d-standard-64"
  auction_machine_type               = ""    # Example: "n2d-standard-64"
  collector_machine_type             = ""    # Example: "e2-micro"
  min_replicas_per_service_region    = 1     # Example: 1
  max_replicas_per_service_region    = 5     # Example: 5
  vm_startup_delay_seconds           = 200   # Example: 200
  cpu_utilization_percent            = 0.6   # Example: 0.6
  use_confidential_space_debug_image = false # Example: false
  tee_impersonate_service_accounts   = "staging-a-opallowedusr@coordinator1.iam.gserviceaccount.com,staging-b-opallowedusr@coordinator2.iam.gserviceaccount.com"
  collector_service_port             = 4317
}
