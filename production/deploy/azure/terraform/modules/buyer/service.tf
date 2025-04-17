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

provider "kubernetes" {
  host                   = module.aks.kube_config_host
  client_certificate     = base64decode(module.aks.kube_config_client_certificate)
  client_key             = base64decode(module.aks.kube_config_client_key)
  cluster_ca_certificate = base64decode(module.aks.kube_config_cluster_ca_certificate)
}

provider "helm" {
  debug = true
  kubernetes {
    host                   = module.aks.kube_config_host
    client_certificate     = base64decode(module.aks.kube_config_client_certificate)
    client_key             = base64decode(module.aks.kube_config_client_key)
    cluster_ca_certificate = base64decode(module.aks.kube_config_cluster_ca_certificate)
  }
}

module "networking" {
  source                = "../../components/networking"
  region                = var.region
  operator              = var.operator
  environment           = var.environment
  resource_group_name   = var.resource_group_name
  vnet_cidr             = var.vnet_cidr
  default_subnet_cidr   = var.default_subnet_cidr
  aks_subnet_cidr       = var.aks_subnet_cidr
  cg_subnet_cidr        = var.cg_subnet_cidr
  agfc_subnet_cidr      = var.agfc_subnet_cidr
  private_dns_zone_name = var.private_dns_zone_name
}

module "aks" {
  source               = "../../components/aks"
  region               = var.region
  operator             = var.operator
  environment          = var.environment
  resource_group_name  = var.resource_group_name
  aks_subnet_id        = module.networking.aks-subnet_id
  resource_group_id    = var.resource_group_id
  vnet_id              = module.networking.vnet_id
  monitor_workspace_id = var.monitor_workspace_id
  aks_service_cidr     = var.aks_service_cidr
  aks_dns_service_ip   = var.aks_dns_service_cidr
  namespace            = var.namespace
}

module "iam" {
  source                  = "../../components/iam"
  operator                = var.operator
  environment             = var.environment
  region                  = var.region
  resource_group_name     = var.resource_group_name
  resource_group_id       = var.resource_group_id
  node_resource_group_id  = module.aks.node_resource_group_id
  vnet_id                 = module.networking.vnet_id
  agfc_subnet_id          = module.networking.agfc-subnet_id
  principal_id            = module.aks.principal_id
  kubelet_principal_id    = module.aks.kubelet_principal_id
  oidc_issuer_url         = module.aks.oidc_issuer_url
  acr_id                  = "/subscriptions/5f35a170-558f-4685-99a4-aa9014412fca/resourceGroups/bna-docker-images/providers/Microsoft.ContainerRegistry/registries/bnadev"
  dns_zone_name           = var.dns_zone_name
  dns_zone_resource_group = var.dns_zone_resource_group
}

module "virtual_node" {
  source                                = "../../components/virtual_node"
  aks_cluster_name                      = module.aks.name
  resource_group_name                   = var.resource_group_name
  region                                = var.region
  operator                              = var.operator
  environment                           = var.environment
  vn_replica_count                      = var.vn_replica_count
  vn_admission_controller_replica_count = var.vn_admission_controller_replica_count
}


module "regional_load_balancing" {
  source                         = "../../components/regional_load_balancing"
  region                         = var.region
  operator                       = var.operator
  environment                    = var.environment
  resource_group_name            = var.resource_group_name
  subnet_id                      = module.networking.agfc-subnet_id
  traffic_manager_profile_id     = var.global_load_balancer_id
  geo_tie_break_routing_priority = var.geo_tie_break_routing_priority
}

module "services" {
  source       = "../../components/services"
  fe_service   = "bfe"
  fe_grpc_port = var.fe_grpc_port
  service      = "bidding"
  grpc_port    = var.grpc_port
  namespace    = var.namespace
}

module "deployments" {
  source               = "../../components/deployments"
  fe_service           = "bfe"
  fe_replicas          = var.hpa_fe_min_replicas
  fe_image             = "bnadev.azurecr.io/services/buyer_frontend_service:gsv-w-parc"
  image                = "bnadev.azurecr.io/services/bidding_service:gsv-w-parc"
  environment          = var.environment
  operator             = var.operator
  region               = var.region
  zone                 = var.zone
  fe_grpc_port         = var.fe_grpc_port
  grpc_port            = var.grpc_port
  service              = "bidding"
  namespace            = var.namespace
  parc_config_map_hash = module.parc.parc_config_map_hash
  replicas             = var.hpa_min_replicas
}

module "autoscaling" {
  source                     = "../../components/autoscaling"
  namespace                  = var.namespace
  fe_service                 = "bfe"
  service                    = "bidding"
  hpa_fe_min_replicas        = var.hpa_fe_min_replicas
  hpa_fe_max_replicas        = var.hpa_fe_max_replicas
  hpa_fe_avg_cpu_utilization = var.hpa_fe_avg_cpu_utilization
  hpa_min_replicas           = var.hpa_min_replicas
  hpa_max_replicas           = var.hpa_max_replicas
  hpa_avg_cpu_utilization    = var.hpa_avg_cpu_utilization
  frontend_deployment_name   = module.deployments.frontend_deployment_name
  backend_deployment_name    = module.deployments.backend_deployment_name
}

module "otel_collector" {
  source                                        = "../../components/otel_collector"
  region                                        = var.region
  operator                                      = var.operator
  environment                                   = var.environment
  resource_group_name                           = var.resource_group_name
  namespace                                     = var.namespace
  otel_image                                    = "otel/opentelemetry-collector-contrib:latest"
  otel_grpc_port                                = var.otel_grpc_port
  application_insights_otel_connection_string   = var.application_insights_otel_connection_string
  application_insights_otel_instrumentation_key = var.application_insights_otel_instrumentation_key
}

module "parc" {
  source      = "../../components/parc"
  operator    = var.operator
  environment = var.environment
  namespace   = var.namespace
  service     = "parc"
  parc_image  = var.parc_image
  parc_port   = var.parc_port
}

module "tls" {
  source                  = "../../components/tls"
  namespace               = var.namespace
  subscription_id         = var.subscription_id
  cert_email              = var.cert_email
  user_assigned_client_id = module.iam.user_assigned_identity_client_id
  agfc_resource_id        = module.regional_load_balancing.agfc_id
  agfc_fe_name            = module.regional_load_balancing.agfc_fe_name
  fe_grpc_port            = var.fe_grpc_port
  fe_service              = "bfe"
  dns_zone_name           = var.dns_zone_name
}
