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
  environment = "" # Example: "test"
}

provider "aws" {
  region = local.region
}

module "buyer" {
  source                               = "../../modules/buyer"
  environment                          = local.environment
  region                               = local.region
  enclave_debug_mode                   = false # Example: false
  root_domain                          = ""    # Example: "bidding1.com"
  root_domain_zone_id                  = ""    # Example: "Z1011487GET92S4MN4CM"
  certificate_arn                      = ""    # Example: "arn:aws:acm:us-west-1:57473821111:certificate/59ebdcbe-2475-4b70-9079-7a360f5c1111"
  operator                             = ""    # Example: "buyer1"
  bfe_instance_ami_id                  = ""    # Example: "ami-0ea7735ce85ec9cf5"
  bidding_instance_ami_id              = ""    # Example: "ami-0f2f28fc0914f6575"
  instance_type                        = ""    # Example: "c6i.2xlarge"
  enclave_cpu_count                    = 6     # Example: 6
  enclave_memory_mib                   = 12000 # Example: 12000
  bfe_autoscaling_desired_capacity     = 3     # Example: 3
  bfe_autoscaling_max_size             = 5     # Example: 5
  bfe_autoscaling_min_size             = 1     # Example: 1
  bidding_autoscaling_desired_capacity = 3     # Example: 3
  bidding_autoscaling_max_size         = 5     # Example: 5
  bidding_autoscaling_min_size         = 1     # Example: 1

  runtime_flags = {
    BIDDING_PORT        = "50051" # Do not change unless you are modifying the default GCP architecture.
    BUYER_FRONTEND_PORT = "50051" # Do not change unless you are modifying the default GCP architecture.
    BFE_INGRESS_TLS     = "flase" # Do not change unless you are modifying the default GCP architecture.
    BIDDING_EGRESS_TLS  = "true"  # Do not change unless you are modifying the default GCP architecture.

    ENABLE_BIDDING_SERVICE_BENCHMARK   = "" # Example: "false"
    BIDDING_SERVER_ADDR                = "" # Example: "dns:///bidding1.com:443"
    BUYER_KV_SERVER_ADDR               = "" # Example: "https://googleads.g.doubleclick.net/td/bts"
    ENABLE_BUYER_DEBUG_URL_GENERATION  = "" # Example: "false"
    GENERATE_BID_TIMEOUT_MS            = "" # Example: "60000"
    BIDDING_SIGNALS_LOAD_TIMEOUT_MS    = "" # Example: "60000"
    ENABLE_BUYER_FRONTEND_BENCHMARKING = "" # Example: "false"
    CREATE_NEW_EVENT_ENGINE            = "" # Example: "false"
    ENABLE_BIDDING_COMPRESSION         = "" # Example: "true"
    ENABLE_ENCRYPTION                  = "" # Example: "true"
    TELEMETRY_CONFIG                   = "" # Example: "mode: EXPERIMENT"
    TEST_MODE                          = "" # Example: "false"
    JS_URL                             = "" # Example: "https://storage.googleapis.com/test/generateBid.js"
    JS_URL_FETCH_PERIOD_MS             = "" # Example: "3600000"
    JS_TIME_OUT_MS                     = "" # Example: "30000"
    ROMA_TIMEOUT_MS                    = "" # Example: "10000"

    # Coordinator-based attestation flags:
    PUBLIC_KEY_ENDPOINT                        = "" # Example: "https://test.cloudfront.net/v1alpha/publicKeys"
    PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT   = "" # Example: "https://test.execute-api.us-east-1.amazonaws.com/stage/v1alpha/encryptionKeys"
    SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT = "" # Example: "https://test.execute-api.us-east-1.amazonaws.com/stage/v1alpha/encryptionKeys"
    PRIMARY_COORDINATOR_ACCOUNT_IDENTITY       = "" # Example: "arn:aws:iam::574738241111:role/mp-prim-ba_574738241111_coordinator_assume_role"
    SECONDARY_COORDINATOR_ACCOUNT_IDENTITY     = "" # Example: "arn:aws:iam::574738241111:role/mp-sec-ba_574738241111_coordinator_assume_role"
    PRIMARY_COORDINATOR_REGION                 = "" # Example: "us-east-1"
    SECONDARY_COORDINATOR_REGION               = "" # Example: "us-east-1"
    PRIVATE_KEY_CACHE_TTL_SECONDS              = "" # Example: "64800"
    KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS     = "" # Example: "10800"
  }
}
