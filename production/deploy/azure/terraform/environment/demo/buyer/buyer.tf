locals {
  environment     = "demo"
  operator        = "tf"
  region          = "centralindia"
  subscription_id = "7ca35580-fc67-469c-91a7-68b38569ca6e"
  tenant_id       = "72f988bf-86f1-41af-91ab-2d7cd011db47"
}

module "buyer" {
  source          = "../../../modules/buyer"
  environment     = local.environment
  operator        = local.operator
  region          = local.region
  subscription_id = local.subscription_id
  tenant_id       = local.tenant_id
}