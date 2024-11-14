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

variable "operator" {
  description = "Assigned name of an operator in Bidding & Auction system, i.e. seller1, buyer1, buyer2."
  type        = string
}

variable "environment" {
  description = "Assigned environment name to group related resources."
  type        = string
}

variable "service" {
  description = "One of: bidding, auction, bfe, sfe"
  type        = string
}

variable "app_mesh_id" {
  description = "ID of the app mesh to which we are adding this cloud map"
  type        = string
}

variable "app_mesh_name" {
  description = "name of the app mesh to which we are adding this cloud map"
  type        = string
}

variable "cloud_map_private_dns_namespace_id" {
  description = "ID of the cloud map private DNS namespace in which we are making this cloud map"
  type        = string
}

variable "cloud_map_private_dns_namespace_name" {
  description = "Name of the cloud map private DNS namespace in which we are making this cloud map"
  type        = string
}

variable "root_domain" {
  description = "Root domain for APIs."
  type        = string
}

variable "server_instance_role_name" {
  description = "Role for server EC2 instance profile."
  type        = string
}

variable "backend_virtual_service_name" {
  description = "Name of the AppMesh virtual service to which this service sends outbound traffic"
  type        = string
}

variable "backend_virtual_service_port" {
  description = "Port of the AppMesh virtual service to which this service sends outbound traffic"
  type        = number
}

variable "backend_virtual_service_private_certificate_arn" {
  description = "ARN of the PCA cert of the AppMesh virtual service to which this service sends outbound traffic"
  type        = string
}

variable "service_port" {
  description = "Port on which this service recieves outbound traffic"
  type        = number
}

variable "backend_service" {
  description = "name of backend service to which this service reaches out. For example, for BFE, this should be 'bidding', or for SFE this should be 'auction'"
  type        = string
}

variable "root_domain_zone_id" {
  description = "Zone id for the root domain."
  type        = string
}

variable "healthcheck_interval_sec" {
  description = "Amount of time between health check intervals in seconds."
  type        = number
}

variable "healthcheck_timeout_sec" {
  description = "Amount of time to wait for a health check response in seconds."
  type        = number
}

variable "healthcheck_healthy_threshold" {
  description = "Consecutive health check successes required to be considered healthy."
  type        = number
}

variable "healthcheck_unhealthy_threshold" {
  description = "Consecutive health check failures required to be considered unhealthy."
  type        = number
}

variable "use_tls_with_mesh" {
  type        = bool
  description = "Whether to use TLS-encrypted communication between service mesh envoy sidecars."
}

variable "region" {
  description = "AWS region in which services have been created"
  type        = string
}
