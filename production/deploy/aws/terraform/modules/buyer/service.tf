/**
 * Copyright 2022 Google LLC
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

terraform {
  required_providers {
    aws = {
      source = "hashicorp/aws"
    }
  }
}

################ Common Setup ################

module "iam_roles" {
  source      = "../../services/iam_roles"
  environment = var.environment
  operator    = var.operator
  region      = var.region
}

module "networking" {
  source         = "../../services/networking"
  operator       = var.operator
  environment    = var.environment
  vpc_cidr_block = var.vpc_cidr_block
  forced_availability_zones = var.forced_availability_zones
}

module "security_groups" {
  source      = "../../services/security_groups"
  environment = var.environment
  operator    = var.operator
  vpc_id      = module.networking.vpc_id
}

module "backend_services" {
  source                          = "../../services/backend_services"
  region                          = var.region
  environment                     = var.environment
  operator                        = var.operator
  vpc_endpoint_route_table_ids    = module.networking.private_route_table_ids
  vpc_endpoint_sg_id              = module.security_groups.vpc_endpoint_security_group_id
  vpc_endpoint_subnet_ids         = module.networking.private_subnet_ids
  vpc_gateway_endpoint_services   = var.vpc_gateway_endpoint_services
  vpc_id                          = module.networking.vpc_id
  vpc_interface_endpoint_services = var.vpc_interface_endpoint_services
  server_instance_role_arn        = module.iam_roles.instance_role_arn
}

module "security_group_rules" {
  source                            = "../../services/security_group_rules"
  region                            = var.region
  operator                          = var.operator
  environment                       = var.environment
  server_instance_port              = var.server_port
  vpc_id                            = module.networking.vpc_id
  elb_security_group_id             = module.security_groups.elb_security_group_id
  instances_security_group_id       = module.security_groups.instance_security_group_id
  vpce_security_group_id            = module.security_groups.vpc_endpoint_security_group_id
  gateway_endpoints_prefix_list_ids = module.backend_services.gateway_endpoints_prefix_list_ids
  use_service_mesh                  = var.use_service_mesh
  tee_kv_servers_port               = var.tee_kv_servers_port
}

module "iam_role_policies" {
  source                    = "../../services/iam_role_policies"
  region                    = var.region
  operator                  = var.operator
  environment               = var.environment
  server_instance_role_name = module.iam_roles.instance_role_name
  autoscaling_group_arns    = [module.autoscaling_bfe.autoscaling_group_arn, module.autoscaling_bidding.autoscaling_group_arn]
  coordinator_role_arns     = var.coordinator_role_arns
}

module "buyer_dashboard" {
  source      = "../../services/dashboards/buyer_dashboard"
  environment = var.environment
  region      = var.region
}

module "inference_dashboard" {
  source      = "../../services/dashboards/inference_dashboard"
  environment = var.environment
  region      = var.region
}

module "k_anon_dashboard" {
  source      = "../../services/dashboards/k_anon_dashboard"
  environment = var.environment
  region      = var.region
}

module "buyer_app_mesh" {
  # Only create if using service mesh
  count = var.use_service_mesh ? 1 : 0

  source      = "../../services/app_mesh"
  operator    = var.operator
  environment = var.environment
  vpc_id      = module.networking.vpc_id
}

################ Bidding operator Setup ################

# Latest AMIs per service, filtered by workspace
data "aws_ami_ids" "bidding_amis" {
  owners         = ["self"]
  sort_ascending = false
  filter {
    name   = "name"
    values = ["bidding_service-*"]
  }

  filter {
    name   = "tag:build_env"
    values = [terraform.workspace]
  }
}

module "bidding_mesh_service" {
  # Only create if using service mesh
  count = var.use_service_mesh ? 1 : 0

  source                                      = "../../services/backend_mesh_service"
  operator                                    = var.operator
  environment                                 = var.environment
  service                                     = "bidding"
  app_mesh_id                                 = module.buyer_app_mesh[0].app_mesh_id
  app_mesh_name                               = module.buyer_app_mesh[0].app_mesh_name
  root_domain                                 = var.root_domain
  cloud_map_private_dns_namespace_id          = module.buyer_app_mesh[0].cloud_map_private_dns_namespace_id
  cloud_map_private_dns_namespace_name        = module.buyer_app_mesh[0].cloud_map_private_dns_namespace_name
  server_instance_role_name                   = module.iam_roles.instance_role_name
  business_org_for_cert_auth                  = var.business_org_for_cert_auth
  country_for_cert_auth                       = var.country_for_cert_auth
  state_for_cert_auth                         = var.state_for_cert_auth
  locality_for_cert_auth                      = var.locality_for_cert_auth
  org_unit_for_cert_auth                      = var.org_unit_for_cert_auth
  service_port                                = var.server_port
  root_domain_zone_id                         = var.root_domain_zone_id
  healthcheck_interval_sec                    = var.healthcheck_interval_sec
  healthcheck_timeout_sec                     = var.healthcheck_timeout_sec
  healthcheck_healthy_threshold               = var.healthcheck_healthy_threshold
  healthcheck_unhealthy_threshold             = var.healthcheck_unhealthy_threshold
  use_tls_with_mesh                           = var.use_tls_with_mesh
  kv_server_virtual_service_name              = var.kv_server_virtual_service_name
  ad_retrieval_kv_server_virtual_service_name = var.ad_retrieval_kv_server_virtual_service_name
  region                                      = var.region
}

module "load_balancing_bidding" {
  # Only create if not using service mesh
  count = var.use_service_mesh ? 0 : 1

  source                          = "../../services/load_balancing"
  region                          = var.region
  environment                     = var.environment
  operator                        = var.operator
  service                         = "bidding"
  certificate_arn                 = var.certificate_arn
  elb_subnet_ids                  = module.networking.public_subnet_ids
  server_port                     = var.server_port
  vpc_id                          = module.networking.vpc_id
  elb_security_group_id           = module.security_groups.elb_security_group_id
  root_domain                     = var.root_domain
  root_domain_zone_id             = var.root_domain_zone_id
  healthcheck_healthy_threshold   = var.healthcheck_healthy_threshold
  healthcheck_interval_sec        = var.healthcheck_interval_sec
  healthcheck_unhealthy_threshold = var.healthcheck_unhealthy_threshold
  # Recommended not to change. Ensures internal VPC load balancers for traffic over private network.
  internal = true
}

module "autoscaling_bidding" {
  source                          = "../../services/autoscaling"
  environment                     = var.environment
  operator                        = var.operator
  enclave_debug_mode              = var.enclave_debug_mode
  service                         = "bidding"
  autoscaling_subnet_ids          = var.auto_scaling_use_first_zone ? [module.networking.private_subnet_ids[0]] : module.networking.private_subnet_ids
  instance_ami_id                 = coalesce(var.bidding_instance_ami_id, data.aws_ami_ids.bidding_amis.ids...)
  instance_security_group_id      = module.security_groups.instance_security_group_id
  instance_type                   = var.bidding_instance_type
  target_group_arns               = var.use_service_mesh ? [] : module.load_balancing_bidding[0].target_group_arns
  autoscaling_desired_capacity    = var.bidding_autoscaling_desired_capacity
  autoscaling_max_size            = var.bidding_autoscaling_max_size
  autoscaling_min_size            = var.bidding_autoscaling_min_size
  instance_profile_arn            = module.iam_roles.instance_profile_arn
  enclave_cpu_count               = var.bidding_enclave_cpu_count
  enclave_memory_mib              = var.bidding_enclave_memory_mib
  cloud_map_service_id            = var.use_service_mesh ? module.bidding_mesh_service[0].cloud_map_service_id : ""
  region                          = var.region
  app_mesh_name                   = var.use_service_mesh ? module.buyer_app_mesh[0].app_mesh_name : ""
  virtual_node_name               = var.use_service_mesh ? module.bidding_mesh_service[0].virtual_node_name : ""
  healthcheck_interval_sec        = var.healthcheck_interval_sec
  healthcheck_timeout_sec         = var.healthcheck_timeout_sec
  healthcheck_healthy_threshold   = var.healthcheck_healthy_threshold
  healthcheck_unhealthy_threshold = var.healthcheck_unhealthy_threshold
  healthcheck_grace_period_sec    = var.healthcheck_grace_period_sec
  consented_request_s3_bucket     = var.consented_request_s3_bucket
}

################ Buyer FrontEnd operator Setup ################
# Latest AMIs per service, filtered by workspace
data "aws_ami_ids" "buyer_frontend_amis" {
  owners         = ["self"]
  sort_ascending = false

  filter {
    name   = "name"
    values = ["buyer_frontend_service-*"]
  }
  filter {
    name   = "tag:build_env"
    values = [terraform.workspace]
  }
}

module "bfe_mesh_service" {
  # Only create if using service mesh
  count = var.use_service_mesh ? 1 : 0

  source                                          = "../../services/frontend_mesh_service"
  region                                          = var.region
  operator                                        = var.operator
  environment                                     = var.environment
  service                                         = "bfe"
  app_mesh_id                                     = module.buyer_app_mesh[0].app_mesh_id
  app_mesh_name                                   = module.buyer_app_mesh[0].app_mesh_name
  root_domain                                     = var.root_domain
  cloud_map_private_dns_namespace_id              = module.buyer_app_mesh[0].cloud_map_private_dns_namespace_id
  cloud_map_private_dns_namespace_name            = module.buyer_app_mesh[0].cloud_map_private_dns_namespace_name
  server_instance_role_name                       = module.iam_roles.instance_role_name
  backend_virtual_service_name                    = module.bidding_mesh_service[0].virtual_service_name
  backend_virtual_service_port                    = var.server_port
  backend_virtual_service_private_certificate_arn = (var.use_tls_with_mesh) ? module.bidding_mesh_service[0].acmpca_certificate_authority_arn : "" # This will be empty string for var.use_tls_with_mesh = false anyways.
  service_port                                    = var.server_port
  backend_service                                 = "bidding"
  root_domain_zone_id                             = var.root_domain_zone_id
  healthcheck_interval_sec                        = var.healthcheck_interval_sec
  healthcheck_timeout_sec                         = var.healthcheck_timeout_sec
  healthcheck_healthy_threshold                   = var.healthcheck_healthy_threshold
  healthcheck_unhealthy_threshold                 = var.healthcheck_unhealthy_threshold
  use_tls_with_mesh                               = var.use_tls_with_mesh
}

module "load_balancing_bfe" {
  source                          = "../../services/load_balancing"
  region                          = var.region
  environment                     = var.environment
  operator                        = var.operator
  service                         = "bfe"
  certificate_arn                 = var.certificate_arn
  elb_subnet_ids                  = module.networking.public_subnet_ids
  server_port                     = var.server_port
  vpc_id                          = module.networking.vpc_id
  elb_security_group_id           = module.security_groups.elb_security_group_id
  root_domain                     = var.root_domain
  root_domain_zone_id             = var.root_domain_zone_id
  healthcheck_healthy_threshold   = var.healthcheck_healthy_threshold
  healthcheck_interval_sec        = var.healthcheck_interval_sec
  healthcheck_unhealthy_threshold = var.healthcheck_unhealthy_threshold
}

module "autoscaling_bfe" {
  source                          = "../../services/autoscaling"
  environment                     = var.environment
  operator                        = var.operator
  enclave_debug_mode              = var.enclave_debug_mode
  service                         = "bfe"
  autoscaling_subnet_ids          = var.auto_scaling_use_first_zone ? [module.networking.private_subnet_ids[0]] : module.networking.private_subnet_ids
  instance_ami_id                 = coalesce(var.bfe_instance_ami_id, data.aws_ami_ids.buyer_frontend_amis.ids...)
  instance_security_group_id      = module.security_groups.instance_security_group_id
  instance_type                   = var.bfe_instance_type
  target_group_arns               = module.load_balancing_bfe.target_group_arns
  autoscaling_desired_capacity    = var.bfe_autoscaling_desired_capacity
  autoscaling_max_size            = var.bfe_autoscaling_max_size
  autoscaling_min_size            = var.bfe_autoscaling_min_size
  instance_profile_arn            = module.iam_roles.instance_profile_arn
  enclave_cpu_count               = var.bfe_enclave_cpu_count
  enclave_memory_mib              = var.bfe_enclave_memory_mib
  cloud_map_service_id            = var.use_service_mesh ? module.bfe_mesh_service[0].cloud_map_service_id : ""
  region                          = var.region
  app_mesh_name                   = var.use_service_mesh ? module.buyer_app_mesh[0].app_mesh_name : ""
  virtual_node_name               = var.use_service_mesh ? module.bfe_mesh_service[0].virtual_node_name : ""
  healthcheck_interval_sec        = var.healthcheck_interval_sec
  healthcheck_timeout_sec         = var.healthcheck_timeout_sec
  healthcheck_healthy_threshold   = var.healthcheck_healthy_threshold
  healthcheck_unhealthy_threshold = var.healthcheck_unhealthy_threshold
  healthcheck_grace_period_sec    = var.healthcheck_grace_period_sec
  consented_request_s3_bucket     = var.consented_request_s3_bucket
}

################ Parameter Setup ################

resource "aws_ssm_parameter" "runtime_flags" {
  for_each = var.runtime_flags

  name      = "${var.operator}-${var.environment}-${each.key}"
  type      = "String"
  value     = each.value
  tier      = "Intelligent-Tiering"
  overwrite = true
}
