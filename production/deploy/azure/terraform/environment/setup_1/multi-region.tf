/**
 * Copyright 2025 Google LLC
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
  environment          = terraform.workspace
  namespace            = var.use_default_namespace ? "default" : local.environment
  sfe_ips              = [module.seller_eastus.fe_service_ip, module.seller_westus.fe_service_ip]
  auction_ips          = [module.seller_eastus.service_ip, module.seller_westus.service_ip]
  bfe_ips              = [module.buyer_eastus.fe_service_ip]
  bidding_ips          = [module.buyer_eastus.service_ip]
  seller_collector_ips = [module.seller_eastus.collector_ip, module.seller_westus.collector_ip]
  buyer_collector_ips  = [module.buyer_eastus.collector_ip]
  seller_parc_ips      = [module.seller_eastus.parc_ip, module.seller_westus.parc_ip]
  buyer_parc_ips       = [module.buyer_eastus.parc_ip]
}

terraform {
  required_version = ">= 1.2.3"

  required_providers {
    azurerm = {
      source  = "hashicorp/azurerm"
      version = "~> 4.3.0"
    }
    kubernetes = {
      source  = "hashicorp/kubernetes"
      version = "~> 2.0"
    }
    helm = {
      source  = "hashicorp/helm"
      version = "~> 2.0"
    }
  }

  # Comment out whole backend block if you want to store the tf state locally: not recommended because if you lost your local state, it will be painful to delete the whole stack manually
  # Follow this link to setup remote state: https://learn.microsoft.com/en-us/azure/developer/terraform/store-state-in-azure-storage?tabs=azure-cli
  backend "azurerm" {
    resource_group_name  = "gsvrg"
    storage_account_name = "gsvsa"
    container_name       = "gsvsc-aci"

    key = "terraform.tfstate"
  }
}

# Configure the Microsoft Azure Provider
provider "azurerm" {
  features {
    resource_group {
      prevent_deletion_if_contains_resources = false
    }
  }
  subscription_id = var.subscription_id
}

# Creates a resource group for each Seller operator
module "seller_resource_group" {
  for_each    = toset(var.seller_operators)
  source      = "../../components/resource_group"
  region      = var.seller_regions[0]
  operator    = each.value
  environment = local.environment
}

# Creates a Global Load Balancer (Traffic Manager) for each Seller operator
module "seller_global_load_balancing" {
  for_each                = module.seller_resource_group
  source                  = "../../components/global_load_balancing"
  resource_group_name     = each.value.name
  operator                = each.value.operator
  environment             = local.environment
  dns_zone_name           = var.seller_dns_zone_name
  dns_zone_resource_group = var.seller_dns_zone_resource_group
}

# Creates and Populates a Private DNS Zone for each Seller operator
module "seller_dns" {
  for_each              = module.seller_resource_group
  source                = "../../components/dns"
  resource_group_name   = each.value.name
  private_dns_zone_name = var.seller_private_dns_zone_name
  fe_service_ips        = local.sfe_ips
  service_ips           = local.auction_ips
  collector_ips         = local.seller_collector_ips
  parc_ips              = local.seller_parc_ips
  fe_service            = "sfe"
  service               = "auction"
}

# Creates OTel metrics dashboards for each Seller operator
module "seller_dashboards" {
  for_each            = module.seller_resource_group
  source              = "../../components/dashboards"
  resource_group_name = each.value.name
  operator            = each.value.operator
  environment         = local.environment
  region              = each.value.region
}

data "azurerm_client_config" "current" {}

module "seller_eastus" {
  source                                        = "../../modules/seller"
  resource_group_name                           = module.seller_resource_group[var.seller_operators[0]].name
  resource_group_id                             = module.seller_resource_group[var.seller_operators[0]].id
  region                                        = "eastus"
  operator                                      = module.seller_resource_group[var.seller_operators[0]].operator
  environment                                   = local.environment
  subscription_id                               = data.azurerm_client_config.current.subscription_id
  tenant_id                                     = data.azurerm_client_config.current.tenant_id
  cert_email                                    = var.seller_cert_email
  global_load_balancer_id                       = module.seller_global_load_balancing[var.seller_operators[0]].global_load_balancer_id
  geo_tie_break_routing_priority                = var.seller_regional_config["eastus"].geo_tie_break_routing_priority
  private_dns_zone_name                         = var.seller_private_dns_zone_name
  zone                                          = var.seller_regional_config["eastus"].availability_zones[0]
  namespace                                     = local.namespace
  vnet_cidr                                     = "${var.seller_regional_config["eastus"].vnet_prefix_high_bit}.0.0.0/12"
  default_subnet_cidr                           = "${var.seller_regional_config["eastus"].vnet_prefix_high_bit}.0.0.0/24"
  aks_subnet_cidr                               = "${var.seller_regional_config["eastus"].vnet_prefix_high_bit}.1.0.0/16"
  cg_subnet_cidr                                = "${var.seller_regional_config["eastus"].vnet_prefix_high_bit}.2.0.0/16"
  agfc_subnet_cidr                              = "${var.seller_regional_config["eastus"].vnet_prefix_high_bit}.3.0.0/16"
  aks_service_cidr                              = "${var.seller_regional_config["eastus"].vnet_prefix_high_bit}.4.0.0/16"
  aks_dns_service_cidr                          = "${var.seller_regional_config["eastus"].vnet_prefix_high_bit}.4.0.10"
  vn_replica_count                              = var.seller_vn_replica_count
  vn_admission_controller_replica_count         = var.seller_vn_admission_controller_replica_count
  hpa_fe_min_replicas                           = var.seller_hpa_fe_min_replicas
  hpa_fe_max_replicas                           = var.seller_hpa_fe_max_replicas
  hpa_fe_avg_cpu_utilization                    = var.seller_hpa_fe_avg_cpu_utilization
  hpa_min_replicas                              = var.seller_hpa_min_replicas
  hpa_max_replicas                              = var.seller_hpa_max_replicas
  hpa_avg_cpu_utilization                       = var.seller_hpa_avg_cpu_utilization
  dns_zone_name                                 = var.seller_dns_zone_name
  dns_zone_resource_group                       = module.seller_resource_group[var.seller_operators[0]].name
  fe_grpc_port                                  = var.seller_fe_grpc_port
  fe_http_port                                  = var.seller_fe_http_port
  grpc_port                                     = var.seller_grpc_port
  otel_grpc_port                                = var.seller_otel_grpc_port
  application_insights_otel_connection_string   = module.seller_dashboards[var.seller_operators[0]].application_insights_otel_connection_string
  application_insights_otel_instrumentation_key = module.seller_dashboards[var.seller_operators[0]].application_insights_otel_instrumentation_key
  monitor_workspace_id                          = module.seller_dashboards[var.seller_operators[0]].monitor_workspace_id
  parc_image                                    = var.seller_parc_image
  parc_port                                     = var.seller_parc_port
}

# Creates a resource group for each Buyer operator
module "buyer_resource_group" {
  for_each    = toset(var.buyer_operators)
  source      = "../../components/resource_group"
  region      = var.buyer_regions[0]
  operator    = each.value
  environment = local.environment
}

# Creates a Global Load Balancer (Traffic Manager) for each Buyer operator
module "buyer_global_load_balancing" {
  for_each                = module.buyer_resource_group
  source                  = "../../components/global_load_balancing"
  resource_group_name     = each.value.name
  operator                = each.value.operator
  environment             = local.environment
  dns_zone_name           = var.buyer_dns_zone_name
  dns_zone_resource_group = var.buyer_dns_zone_resource_group
}

# Creates and Populates a Private DNS Zone for each Buyer operator
module "buyer_dns" {
  for_each              = module.buyer_resource_group
  source                = "../../components/dns"
  resource_group_name   = each.value.name
  private_dns_zone_name = var.buyer_private_dns_zone_name
  fe_service_ips        = local.bfe_ips
  service_ips           = local.bidding_ips
  collector_ips         = local.buyer_collector_ips
  parc_ips              = local.buyer_parc_ips
  fe_service            = "bfe"
  service               = "bidding"
}

# Creates a OTel metrics dashboards for each Buyer operator
module "buyer_dashboards" {
  for_each            = module.buyer_resource_group
  source              = "../../components/dashboards"
  resource_group_name = each.value.name
  operator            = each.value.operator
  environment         = local.environment
  region              = each.value.region
}

# In order to have multi-buyer support using terraform, it is important to make another module similar to the seller module, and to add the operator to the operators list above.
# This will create a new resource_group, global_load_balancer, and separate private_dns_zone for each buyer or seller.
module "buyer_eastus" {
  source                                        = "../../modules/buyer"
  resource_group_name                           = module.buyer_resource_group[var.buyer_operators[0]].name
  resource_group_id                             = module.buyer_resource_group[var.buyer_operators[0]].id
  region                                        = "eastus"
  operator                                      = module.buyer_resource_group[var.buyer_operators[0]].operator
  environment                                   = local.environment
  subscription_id                               = data.azurerm_client_config.current.subscription_id
  tenant_id                                     = data.azurerm_client_config.current.tenant_id
  cert_email                                    = var.buyer_cert_email
  global_load_balancer_id                       = module.buyer_global_load_balancing[var.buyer_operators[0]].global_load_balancer_id
  geo_tie_break_routing_priority                = var.buyer_regional_config["eastus"].geo_tie_break_routing_priority
  private_dns_zone_name                         = var.buyer_private_dns_zone_name
  zone                                          = var.buyer_regional_config["eastus"].availability_zones[0]
  namespace                                     = local.namespace
  vnet_cidr                                     = "${var.buyer_regional_config["eastus"].vnet_prefix_high_bit}.0.0.0/12"
  default_subnet_cidr                           = "${var.buyer_regional_config["eastus"].vnet_prefix_high_bit}.0.0.0/24"
  aks_subnet_cidr                               = "${var.buyer_regional_config["eastus"].vnet_prefix_high_bit}.1.0.0/16"
  cg_subnet_cidr                                = "${var.buyer_regional_config["eastus"].vnet_prefix_high_bit}.2.0.0/16"
  agfc_subnet_cidr                              = "${var.buyer_regional_config["eastus"].vnet_prefix_high_bit}.3.0.0/16"
  aks_service_cidr                              = "${var.buyer_regional_config["eastus"].vnet_prefix_high_bit}.4.0.0/16"
  aks_dns_service_cidr                          = "${var.buyer_regional_config["eastus"].vnet_prefix_high_bit}.4.0.10"
  vn_replica_count                              = var.buyer_vn_replica_count
  vn_admission_controller_replica_count         = var.buyer_vn_admission_controller_replica_count
  hpa_fe_min_replicas                           = var.buyer_hpa_fe_min_replicas
  hpa_fe_max_replicas                           = var.buyer_hpa_fe_max_replicas
  hpa_fe_avg_cpu_utilization                    = var.buyer_hpa_fe_avg_cpu_utilization
  hpa_min_replicas                              = var.buyer_hpa_min_replicas
  hpa_max_replicas                              = var.buyer_hpa_max_replicas
  hpa_avg_cpu_utilization                       = var.buyer_hpa_avg_cpu_utilization
  dns_zone_name                                 = var.buyer_dns_zone_name
  dns_zone_resource_group                       = module.buyer_resource_group[var.buyer_operators[0]].name
  fe_grpc_port                                  = var.buyer_fe_grpc_port
  grpc_port                                     = var.buyer_grpc_port
  otel_grpc_port                                = var.buyer_otel_grpc_port
  application_insights_otel_connection_string   = module.buyer_dashboards[var.buyer_operators[0]].application_insights_otel_connection_string
  application_insights_otel_instrumentation_key = module.buyer_dashboards[var.buyer_operators[0]].application_insights_otel_instrumentation_key
  monitor_workspace_id                          = module.buyer_dashboards[var.buyer_operators[0]].monitor_workspace_id
  parc_image                                    = var.buyer_parc_image
  parc_port                                     = var.buyer_parc_port
}

# In order to have multi-regional support using terraform, it is important to make another module similar to the seller module, and to add the region to the regions list in the local.regions variable.
# This will create a new AKS cluster and the necessary resources in order to have multiple regional clusters in the SAME resource group.
# The differences between multi-buyer and multi-regional is that each buyer will have completely separate resources within their own resource group, while the multi-regional will share resource_group, global_load_balancing, and a private DNS zone.
module "seller_westus" {
  source                                        = "../../modules/seller"
  resource_group_name                           = module.seller_resource_group[var.seller_operators[0]].name
  resource_group_id                             = module.seller_resource_group[var.seller_operators[0]].id
  region                                        = "westus"
  operator                                      = module.seller_resource_group[var.seller_operators[0]].operator
  environment                                   = local.environment
  subscription_id                               = data.azurerm_client_config.current.subscription_id
  tenant_id                                     = data.azurerm_client_config.current.tenant_id
  cert_email                                    = var.seller_cert_email
  global_load_balancer_id                       = module.seller_global_load_balancing[var.seller_operators[0]].global_load_balancer_id
  geo_tie_break_routing_priority                = var.seller_regional_config["westus"].geo_tie_break_routing_priority
  private_dns_zone_name                         = var.seller_private_dns_zone_name
  zone                                          = var.seller_regional_config["westus"].availability_zones[0]
  namespace                                     = local.namespace
  vnet_cidr                                     = "${var.seller_regional_config["westus"].vnet_prefix_high_bit}.0.0.0/12"
  default_subnet_cidr                           = "${var.seller_regional_config["westus"].vnet_prefix_high_bit}.0.0.0/24"
  aks_subnet_cidr                               = "${var.seller_regional_config["westus"].vnet_prefix_high_bit}.1.0.0/16"
  cg_subnet_cidr                                = "${var.seller_regional_config["westus"].vnet_prefix_high_bit}.2.0.0/16"
  agfc_subnet_cidr                              = "${var.seller_regional_config["westus"].vnet_prefix_high_bit}.3.0.0/16"
  aks_service_cidr                              = "${var.seller_regional_config["westus"].vnet_prefix_high_bit}.4.0.0/16"
  aks_dns_service_cidr                          = "${var.seller_regional_config["westus"].vnet_prefix_high_bit}.4.0.10"
  vn_replica_count                              = var.seller_vn_replica_count
  vn_admission_controller_replica_count         = var.seller_vn_admission_controller_replica_count
  hpa_fe_min_replicas                           = var.seller_hpa_fe_min_replicas
  hpa_fe_max_replicas                           = var.seller_hpa_fe_max_replicas
  hpa_fe_avg_cpu_utilization                    = var.seller_hpa_fe_avg_cpu_utilization
  hpa_min_replicas                              = var.seller_hpa_min_replicas
  hpa_max_replicas                              = var.seller_hpa_max_replicas
  hpa_avg_cpu_utilization                       = var.seller_hpa_avg_cpu_utilization
  dns_zone_name                                 = var.seller_dns_zone_name
  dns_zone_resource_group                       = module.seller_resource_group[var.seller_operators[0]].name
  fe_grpc_port                                  = var.seller_fe_grpc_port
  fe_http_port                                  = var.seller_fe_http_port
  grpc_port                                     = var.seller_grpc_port
  otel_grpc_port                                = var.seller_otel_grpc_port
  application_insights_otel_connection_string   = module.seller_dashboards[var.seller_operators[0]].application_insights_otel_connection_string
  application_insights_otel_instrumentation_key = module.seller_dashboards[var.seller_operators[0]].application_insights_otel_instrumentation_key
  monitor_workspace_id                          = module.seller_dashboards[var.seller_operators[0]].monitor_workspace_id
  parc_image                                    = var.seller_parc_image
  parc_port                                     = var.seller_parc_port
}
