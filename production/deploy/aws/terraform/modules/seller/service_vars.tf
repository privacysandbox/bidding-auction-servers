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

variable "use_service_mesh" {
  description = "use mesh if true, else if false use load balancers"
  type        = bool
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
variable "sfe_instance_type" {
  description = "Hardware and OS configuration for the SFE EC2 instance."
  type        = string
}

variable "auction_instance_type" {
  description = "Hardware and OS configuration for the Auction EC2 instance."
  type        = string
}

variable "sfe_instance_ami_id" {
  description = "Seller FrontEnd operator Amazon Machine Image to run on EC2 instance."
  type        = string
}
variable "auction_instance_ami_id" {
  description = "Auction operator Amazon Machine Image to run on EC2 instance."
  type        = string
}

# Variables related to server configuration.

variable "envoy_port" {
  description = "Envoy port on which the enclave listens for TCP connections."
  type        = number
  default     = 51052
}

variable "server_port" {
  description = "Port on which the enclave listens for TCP connections."
  type        = number
  default     = 50051

}
variable "sfe_enclave_cpu_count" {
  description = "The number of vcpus to allocate to the SFE enclave."
  type        = number
}
variable "sfe_enclave_memory_mib" {
  description = "Amount of memory to allocate to the SFE enclave."
  type        = number
}
variable "auction_enclave_cpu_count" {
  description = "The number of vcpus to allocate to the Auction enclave."
  type        = number
}
variable "auction_enclave_memory_mib" {
  description = "Amount of memory to allocate to the Auction enclave."
  type        = number
}


variable "sfe_autoscaling_desired_capacity" {
  type = number
}

variable "sfe_autoscaling_max_size" {
  type = number
}

variable "sfe_autoscaling_min_size" {
  type = number
}

variable "auction_autoscaling_desired_capacity" {
  type = number
}

variable "auction_autoscaling_max_size" {
  type = number

}

variable "auction_autoscaling_min_size" {
  type = number

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

variable "envoy_healthcheck_path" {
  description = "Healthcheck path when querying the envoy operator for health."
  type        = string
  default     = "/healthcheck"
}

# Variables related to health checks
variable "healthcheck_interval_sec" {
  description = "Amount of time between health check intervals in seconds."
  type        = number
  default     = 7
}
variable "healthcheck_timeout_sec" {
  description = "Amount of time to wait for a health check response in seconds."
  type        = number
  default     = 5
}
variable "healthcheck_grace_period_sec" {
  description = "Amount of time to wait for service inside enclave to start up before starting health checks, in seconds."
  type        = number
  default     = 60
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
  description = "Seller runtime flags. Must exactly match flags specified in <project root>/services/(auction_service|seller_frontend_service)/runtime_flags.h"
}

variable "business_org_for_cert_auth" {
  description = "Name of your business organization, for the private certificate authority"
  type        = string
}

variable "country_for_cert_auth" {
  description = "Country of your business organization, for the private certificate authority"
  type        = string
}

variable "state_for_cert_auth" {
  description = "State or province where your business organization is located, for the private certificate authority"
  type        = string
}

variable "org_unit_for_cert_auth" {
  description = "Name of your particular unit in your business organization, for the private certificate authority"
  type        = string
}

variable "locality_for_cert_auth" {
  description = "Locality where your business organization is located, for the private certificate authority"
  type        = string
}

variable "use_tls_with_mesh" {
  type        = bool
  description = "Whether to use TLS-encrypted communication between service mesh envoy sidecars."
}

variable "kv_server_virtual_service_name" {
  description = "Full name of the virtual service for the KV server. Unused for seller module for now; may be used in future."
  type        = string
  default     = "unused"
}

variable "consented_request_s3_bucket" {
  description = "s3 bucket to export event message for consented request"
  type        = string
}

variable "tee_kv_servers_port" {
  description = "Port on which the TEE KV server accepts connections."
  type        = number
}
