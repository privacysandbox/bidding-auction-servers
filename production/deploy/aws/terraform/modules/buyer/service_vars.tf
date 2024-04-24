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

# Variables related to environment configuration.
variable "environment" {
  description = "Assigned environment name to group related resources."
  type        = string
  validation {
    condition     = length(var.environment) <= 10
    error_message = "Due to current naming scheme limitations, environment must not be longer than 10."
  }
}
variable "region" {
  description = "AWS region to deploy to."
  type        = string
}

# TODO(b/260100326): update key to service_operator
variable "operator" {
  description = "operator name"
  type        = string
}

# Variables related to network, dns and certs configuration.
variable "vpc_cidr_block" {
  description = "CIDR range for the VPC where auction server will be deployed."
  type        = string
  default     = "10.0.0.0/16"
}
variable "root_domain" {
  description = "Root domain for APIs."
  type        = string
}
variable "root_domain_zone_id" {
  description = "Zone id for the root domain."
  type        = string
}
variable "certificate_arn" {
  description = "ARN for a certificate to be attached to the ALB listener."
  type        = string
}

# Variables related to EC2 instances.
variable "bfe_instance_type" {
  description = "Hardware and OS configuration for the BFE EC2 instance."
  type        = string
  default     = "c6i.2xlarge" # Recommend at least c6i.12xlarge for AdTechs testing or load testing.
}

variable "bidding_instance_type" {
  description = "Hardware and OS configuration for the Bidding EC2 instance."
  type        = string
  default     = "c6i.2xlarge" # Recommend at least c6i.12xlarge for AdTechs testing or load testing.
}

variable "bfe_instance_ami_id" {
  description = "Buyer FrontEnd operator Amazon Machine Image to run on EC2 instance."
  type        = string
  default     = "ami-0ea7735ce85ec9cf5"
}
variable "bidding_instance_ami_id" {
  description = "Bidding operator Amazon Machine Image to run on EC2 instance."
  type        = string
  default     = "ami-0f2f28fc0914f6575"
}

# Variables related to server configuration.
variable "server_port" {
  description = "Port on which the enclave listens for TCP connections."
  type        = number
  default     = 50051
}
variable "bfe_enclave_cpu_count" {
  description = "The number of vcpus to allocate to the BFE enclave."
  type        = number
  default     = 6
}
variable "bfe_enclave_memory_mib" {
  description = "Amount of memory to allocate to the BFE enclave."
  type        = number
  default     = 12000
}
variable "bidding_enclave_cpu_count" {
  description = "The number of vcpus to allocate to the Bidding enclave."
  type        = number
  default     = 6
}
variable "bidding_enclave_memory_mib" {
  description = "Amount of memory to allocate to the Bidding enclave."
  type        = number
  default     = 12000
}
variable "bfe_autoscaling_desired_capacity" {
  type    = number
  default = 1
}

variable "bfe_autoscaling_max_size" {
  type    = number
  default = 1
}

variable "bfe_autoscaling_min_size" {
  type    = number
  default = 1
}

variable "bidding_autoscaling_desired_capacity" {
  type    = number
  default = 1
}

variable "bidding_autoscaling_max_size" {
  type    = number
  default = 1
}

variable "bidding_autoscaling_min_size" {
  type    = number
  default = 1
}

# Variables related to AWS backend services
variable "vpc_gateway_endpoint_services" {
  description = "List of AWS services to create vpc gateway endpoints for."
  type        = set(string)
  default     = ["s3"]
}
variable "vpc_interface_endpoint_services" {
  description = "List of AWS services to create vpc interface endpoints for."
  type        = set(string)
  default = [
    "ec2",
    "ssm",
    "ec2messages",
    "ssmmessages",
    "autoscaling",
    "monitoring",
    "xray",
  ]
}

# Variables related to health checks
variable "healthcheck_interval_sec" {
  description = "Amount of time between health check intervals in seconds."
  type        = number
  default     = 7
}
variable "healthcheck_healthy_threshold" {
  description = "Consecutive health check successes required to be considered healthy."
  type        = number
  default     = 2
}
variable "healthcheck_unhealthy_threshold" {
  description = "Consecutive health check failures required to be considered unhealthy."
  type        = number
  default     = 2
}

# Variables related to SSH
variable "ssh_source_cidr_blocks" {
  description = "Source ips allowed to send ssh traffic to the ssh instance."
  type        = set(string)
  default     = ["0.0.0.0/0"]
}

variable "ssh_instance_type" {
  description = "type, that is, hardware resource configuration, for EC2 instance"
  type        = string
  default     = "t2.micro"
}

variable "enclave_debug_mode" {
  description = "If true, strats the Nitro enclave with --debug-mode."
  type        = bool
  default     = false
}

variable "runtime_flags" {
  type        = map(string)
  description = "Buyer runtime flags. Must exactly match flags specified in <project root>/services/(bidding_service|buyer_frontend_service)/runtime_flags.h"
}
