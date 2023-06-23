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

variable "mesh" {
  description = "Traffic Director mesh"
  type        = any
}

variable "operator" {
  description = "Operator name used to identify the resource owner."
  type        = string
}

variable "environment" {
  description = "Assigned environment name to group related resources."
  type        = string
}

variable "gcp_project_id" {
  description = "GCP project id."
  type        = string
}

variable "frontend_domain_name" {
  description = "Domain name for global external LB"
  type        = string
}

variable "frontend_dns_zone" {
  description = "DNS zone for the frontend domain"
  type        = string
}

variable "frontend_ip_address" {
  description = "Frontend ip address"
  type        = string
}

variable "frontend_domain_ssl_certificate_id" {
  description = "A GCP ssl certificate id. Example: projects/bas-dev-383721/global/sslCertificates/dev. Used to terminate client-to-external-LB connections."
  type        = string
}

variable "frontend_instance_groups" {
  description = "Frontend instance group URLs created by instance group managers."
  type        = set(string)
}

variable "frontend_service_name" {
  type = string
}

variable "frontend_service_port" {
  description = "The grpc port that receives traffic destined for the frontend service."
  type        = number
}
variable "backend_instance_groups" {
  description = "Backend instance group URsL created by instance group managers."
  type        = set(string)
}

variable "backend_address" {
  description = "gRPC-compatible address. Example: xds:///backend"
  type        = string
}

variable "backend_service_name" {
  type = string
}

variable "backend_service_port" {
  description = "The grpc port that receives traffic destined for the backend service."
  type        = number
}
