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
  environment    = "" # Example: "testing"
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

module "buyer" {
  source               = "../../../modules/buyer"
  environment          = local.environment
  gcp_project_id       = local.gcp_project_id
  bidding_image        = "${local.image_repo}/bidding_service:${local.environment}"        # Image built and uploaded by production/packaging/build_and_test_all_in_docker
  buyer_frontend_image = "${local.image_repo}/buyer_frontend_service:${local.environment}" # Image built and uploaded by production/packaging/build_and_test_all_in_docker

  runtime_flags = {
    BIDDING_PORT        = "50051"          # Do not change unless you are modifying the default GCP architecture.
    BUYER_FRONTEND_PORT = "50051"          # Do not change unless you are modifying the default GCP architecture.
    BIDDING_SERVER_ADDR = "xds:///bidding" # Do not change unless you are modifying the default GCP architecture.
    BFE_INGRESS_TLS     = "true"           # Do not change unless you are modifying the default GCP architecture.
    BIDDING_EGRESS_TLS  = "false"          # Do not change unless you are modifying the default GCP architecture.
    ENABLE_ENCRYPTION   = "true"           # Do not change unless you are testing without encryption.
    TEST_MODE           = "false"          # Do not change unless you are testing without key fetching.

    ENABLE_BIDDING_SERVICE_BENCHMARK   = "" # Example: "false"
    BUYER_KV_SERVER_ADDR               = "" # Example: "https://googleads.g.doubleclick.net/td/bts"
    ENABLE_BUYER_DEBUG_URL_GENERATION  = "" # Example: "false"
    GENERATE_BID_TIMEOUT_MS            = "" # Example: "60000"
    BIDDING_SIGNALS_LOAD_TIMEOUT_MS    = "" # Example: "60000"
    ENABLE_BUYER_FRONTEND_BENCHMARKING = "" # Example: "false"
    CREATE_NEW_EVENT_ENGINE            = "" # Example: "false"
    ENABLE_BIDDING_COMPRESSION         = "" # Example: "true"

    JS_URL                 = "" # Example: "https://storage.googleapis.com/my-bucket/generateBid.js"
    JS_URL_FETCH_PERIOD_MS = "" # Example: "3600000"
    JS_TIME_OUT_MS         = "" # Example: "30000"
    ROMA_TIMEOUT_MS        = "" # Example: "10000"

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
    PRIVATE_KEY_CACHE_TTL_SECONDS                 = "" # Example:  "600000"
    KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS        = "" # Example: "20000"

    BFE_TLS_KEY  = "" # You can either set this here or via a secrets.auto.tfvars.
    BFE_TLS_CERT = "" # You can either set this here or via a secrets.auto.tfvars.
  }

  # Please manually create a Google Cloud domain name, dns zone, and SSL certificate.
  frontend_domain_name               = "" # Example: bfe-gcp.com
  frontend_dns_zone                  = "" # Example: "bfe-gcp-com"
  frontend_domain_ssl_certificate_id = "" # Example: "projects/${local.gcp_project_id}/global/sslCertificates/bfe-${local.environment}"

  operator                           = ""    # Example: "buyer-1"
  regions                            = []    # Example: ["us-central1", "us-west1"]
  service_account_email              = ""    # Example: "terraform-sa@{local.gcp_project_id}.iam.gserviceaccount.com"
  machine_type                       = ""    # Example: "c2d-standard-4"
  min_replicas_per_service_region    = 1     # Example: 1
  max_replicas_per_service_region    = 5     # Example: 5
  vm_startup_delay_seconds           = 200   # Example: 200
  cpu_utilization_percent            = 0.8   # Example: 0.8
  use_confidential_space_debug_image = false # Example: false
  tee_impersonate_service_accounts   = "staging-a-opallowedusr@coordinator1.iam.gserviceaccount.com,staging-b-opallowedusr@coordinator2.iam.gserviceaccount.com"
}
