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

################ Common Setup ################

module "iam_roles" {
  source      = "../../services/iam_roles"
  environment = var.environment
  operator    = var.operator
}

module "iam_groups" {
  source      = "../../services/iam_groups"
  environment = var.environment
  operator    = var.operator
}

module "networking" {
  source   = "../../services/networking"
  operator = var.operator

  environment    = var.environment
  vpc_cidr_block = var.vpc_cidr_block
}

module "security_groups" {
  source      = "../../services/security_groups"
  environment = var.environment
  operator    = var.operator
  vpc_id      = module.networking.vpc_id
}

module "iam_group_policies" {
  source               = "../../services/iam_group_policies"
  operator             = var.operator
  environment          = var.environment
  ssh_users_group_name = module.iam_groups.ssh_users_group_name
  ssh_instance_arn     = module.ssh.ssh_instance_arn
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
  ssh_instance_role_arn           = module.iam_roles.ssh_instance_role_arn
}

module "ssh" {
  source                  = "../../services/ssh"
  environment             = var.environment
  instance_sg_id          = module.security_groups.ssh_security_group_id
  operator                = var.operator
  ssh_instance_subnet_ids = module.networking.public_subnet_ids
  instance_profile_name   = module.iam_roles.ssh_instance_profile_name
  ssh_instance_type       = var.ssh_instance_type
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
  ssh_security_group_id             = module.security_groups.ssh_security_group_id
  vpce_security_group_id            = module.security_groups.vpc_endpoint_security_group_id
  gateway_endpoints_prefix_list_ids = module.backend_services.gateway_endpoints_prefix_list_ids
  ssh_source_cidr_blocks            = var.ssh_source_cidr_blocks
}

module "iam_role_policies" {
  source                    = "../../services/iam_role_policies"
  operator                  = var.operator
  environment               = var.environment
  server_instance_role_name = module.iam_roles.instance_role_name
  ssh_instance_role_name    = module.iam_roles.ssh_instance_role_name
  autoscaling_group_arns    = [module.autoscaling_bfe.autoscaling_group_arn, module.autoscaling_bidding.autoscaling_group_arn]
}

module "buyer_dashboard" {
  source      = "../../services/dashboards/buyer_dashboard"
  environment = var.environment
}

################ Buyer FrontEnd operator Setup ################
module "load_balancing_bfe" {
  source                          = "../../services/load_balancing"
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
  source                       = "../../services/autoscaling"
  environment                  = var.environment
  operator                     = var.operator
  enclave_debug_mode           = var.enclave_debug_mode
  service                      = "bfe"
  autoscaling_subnet_ids       = module.networking.private_subnet_ids
  instance_ami_id              = var.bfe_instance_ami_id
  instance_security_group_id   = module.security_groups.instance_security_group_id
  instance_type                = var.instance_type
  target_group_arns            = module.load_balancing_bfe.target_group_arns
  autoscaling_desired_capacity = var.bfe_autoscaling_desired_capacity
  autoscaling_max_size         = var.bfe_autoscaling_max_size
  autoscaling_min_size         = var.bfe_autoscaling_min_size
  instance_profile_arn         = module.iam_roles.instance_profile_arn
  enclave_cpu_count            = var.enclave_cpu_count
  enclave_memory_mib           = var.enclave_memory_mib
}

################ Bidding operator Setup ################


module "load_balancing_bidding" {
  source                          = "../../services/load_balancing"
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
  source                       = "../../services/autoscaling"
  environment                  = var.environment
  operator                     = var.operator
  enclave_debug_mode           = var.enclave_debug_mode
  service                      = "bidding"
  autoscaling_subnet_ids       = module.networking.private_subnet_ids
  instance_ami_id              = var.bidding_instance_ami_id
  instance_security_group_id   = module.security_groups.instance_security_group_id
  instance_type                = var.instance_type
  target_group_arns            = module.load_balancing_bidding.target_group_arns
  autoscaling_desired_capacity = var.bidding_autoscaling_desired_capacity
  autoscaling_max_size         = var.bidding_autoscaling_max_size
  autoscaling_min_size         = var.bidding_autoscaling_min_size
  instance_profile_arn         = module.iam_roles.instance_profile_arn
  enclave_cpu_count            = var.enclave_cpu_count
  enclave_memory_mib           = var.enclave_memory_mib
}


################ Parameter Setup ################

resource "aws_ssm_parameter" "runtime_flags" {
  for_each = var.runtime_flags

  name      = "${var.operator}-${var.environment}-${each.key}"
  type      = "String"
  value     = each.value
  overwrite = true
}
