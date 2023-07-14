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

module "networking" {
  source                 = "../../services/networking"
  frontend_service       = "sfe"
  operator               = var.operator
  environment            = var.environment
  regions                = var.regions
  collector_service_name = "collector"
}

module "security" {
  source                 = "../../services/security"
  network_id             = module.networking.network_id
  subnets                = module.networking.subnets
  operator               = var.operator
  environment            = var.environment
  collector_service_port = var.collector_service_port
}

module "autoscaling" {
  source                             = "../../services/autoscaling"
  vpc_id                             = module.networking.network_id
  subnets                            = module.networking.subnets
  mesh_name                          = module.networking.mesh.name
  cpu_utilization_percent            = var.cpu_utilization_percent
  service_account_email              = var.service_account_email
  environment                        = var.environment
  operator                           = var.operator
  backend_tee_image                  = var.auction_image
  backend_service_port               = tonumber(var.runtime_flags["AUCTION_PORT"])
  backend_service_name               = "auction"
  frontend_tee_image                 = var.seller_frontend_image
  frontend_service_port              = var.envoy_port
  frontend_service_name              = "sfe"
  collector_service_name             = "collector"
  collector_service_port             = var.collector_service_port
  machine_type                       = var.machine_type
  min_replicas_per_service_region    = var.min_replicas_per_service_region
  max_replicas_per_service_region    = var.max_replicas_per_service_region
  vm_startup_delay_seconds           = var.vm_startup_delay_seconds
  use_confidential_space_debug_image = var.use_confidential_space_debug_image
  tee_impersonate_service_accounts   = var.tee_impersonate_service_accounts
}

module "load_balancing" {
  source                             = "../../services/load_balancing"
  environment                        = var.environment
  operator                           = var.operator
  gcp_project_id                     = var.gcp_project_id
  mesh                               = module.networking.mesh
  frontend_ip_address                = module.networking.frontend_address
  frontend_domain_name               = var.frontend_domain_name
  frontend_dns_zone                  = var.frontend_dns_zone
  frontend_domain_ssl_certificate_id = var.frontend_domain_ssl_certificate_id
  frontend_instance_groups           = module.autoscaling.frontend_instance_groups
  frontend_service_name              = "sfe"
  frontend_service_port              = var.envoy_port
  backend_instance_groups            = module.autoscaling.backend_instance_groups
  backend_address                    = var.runtime_flags["AUCTION_SERVER_HOST"]
  backend_service_name               = "auction"
  backend_service_port               = tonumber(var.runtime_flags["AUCTION_PORT"])
  collector_ip_address               = module.networking.collector_address
  collector_instance_groups          = module.autoscaling.collector_instance_groups
  collector_service_name             = "collector"
  collector_service_port             = var.collector_service_port
}

resource "google_secret_manager_secret" "runtime_flag_secrets" {
  for_each = var.runtime_flags

  secret_id = "${var.operator}-${var.environment}-${each.key}"
  replication {
    automatic = true
  }
}

resource "google_secret_manager_secret_version" "runtime_flag_secret_values" {
  for_each = google_secret_manager_secret.runtime_flag_secrets

  secret      = each.value.id
  secret_data = var.runtime_flags[split("${var.operator}-${var.environment}-", each.value.id)[1]]
}
