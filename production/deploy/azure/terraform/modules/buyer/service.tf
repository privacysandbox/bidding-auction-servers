################ Common Setup ################

locals {
  frontend_service_name = "buyer"
}
module "resource_group" {
  source                = "../../services/resource_group"
  frontend_service_name = local.frontend_service_name
  operator              = var.operator
  environment           = var.environment
  region                = var.region
}

module "networking" {
  source                = "../../services/networking"
  resource_group_name   = module.resource_group.name
  frontend_service_name = local.frontend_service_name
  operator              = var.operator
  environment           = var.environment
  region                = var.region
  # vnet_address_space    = var.vnet_address_space
  # default_subnet_cidr   = var.default_subnet_cidr
  # aks_subnet_cidr       = var.aks_subnet_cidr
  # cg_subnet_cidr        = var.cg_subnet_cidr
}

module "aks" {
  source                = "../../services/aks"
  resource_group_id     = module.resource_group.id
  resource_group_name   = module.resource_group.name
  frontend_service_name = local.frontend_service_name
  operator              = var.operator
  environment           = var.environment
  region                = var.region
  subnet_id             = module.networking.aks_subnet_id
  virtual_network_id    = module.networking.vnet_id
  # kubernetes_version    = var.kubernetes_version
  # service_cidr          = var.service_cidr
  # dns_service_ip        = var.dns_service_ip
}

module "external_dns" {
  source                     = "../../services/external_dns"
  resource_group_id          = module.resource_group.id
  resource_group_name        = module.resource_group.name
  region                     = var.region
  vnet_id                    = module.networking.vnet_id
  vnet_name                  = module.networking.vnet_name
  private_domain_name        = var.private_domain_name
  aks_cluster_name           = module.aks.name
  aks_oidc_issuer_url        = module.aks.oidc_issuer_url
  kubernetes_namespace       = var.externaldns_kubernetes_namespace
  kubernetes_service_account = var.externaldns_kubernetes_service_account
  tenant_id                  = var.tenant_id
  subscription_id            = var.subscription_id
}

