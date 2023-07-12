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

variable "operator" {
  description = "Operator name used to identify the resource owner."
  type        = string
}

variable "regions" {
  description = "Regions to deploy to."
  type        = set(string)
}

variable "gcp_project_id" {
  description = "GCP project id."
  type        = string
}

variable "service_account_email" {
  description = "Service account email address"
  type        = string
}

variable "bidding_image" {
  description = "URL to bidding service TEE Docker image"
  type        = string
}

variable "buyer_frontend_image" {
  description = "URL to bfe service TEE Docker image"
  type        = string
}

variable "frontend_domain_name" {
  description = "Google Cloud Domain name for global external LB"
  type        = string
}

variable "frontend_dns_zone" {
  description = "Google Cloud DNS zone name for the frontend domain"
  type        = string
}

variable "frontend_domain_ssl_certificate_id" {
  description = "A GCP ssl certificate id. Example: projects/bas-dev-383721/global/sslCertificates/dev"
  type        = string
}

variable "machine_type" {
  description = "Must be listed here: https://cloud.google.com/compute/confidential-vm/docs/os-and-machine-type#machine-types"
  type        = string
}

variable "min_replicas_per_service_region" {
  description = "Minimum amount of replicas per each service region (a single managed instance group)."
  type        = number
}

variable "max_replicas_per_service_region" {
  description = "Maximum amount of replicas per each service region (a single managed instance group)."
  type        = number
}
variable "vm_startup_delay_seconds" {
  description = "The time it takes to get a service up and responding to heartbeats (in seconds)."
  type        = number
}

variable "cpu_utilization_percent" {
  description = "CPU utilization percentage across an instance group required for autoscaler to add instances."
  type        = number
}

variable "use_confidential_space_debug_image" {
  description = "If true, use the Confidential space debug image. Else use the prod image, which does not allow SSH. The images containing the service logic will run on top of this image and have their own prod and debug builds."
  type        = bool
  default     = false
}

variable "runtime_flags" {
  type        = map(string)
  description = "Buyer runtime flags. Must exactly match flags specified in <project root>/services/(bidding_service|buyer_frontend_service)/runtime_flags.h"
}

variable "tee_impersonate_service_accounts" {
  description = "Comma separated list of service accounts (by email) the TEE should impersonate."
  type        = string
  default     = ""
}

variable "collector_service_port" {
  description = "The grpc port that receives traffic destined for the OpenTelemetry collector."
  type        = number
}
