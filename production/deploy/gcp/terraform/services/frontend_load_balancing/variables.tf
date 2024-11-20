/**
 * Copyright 2024 Google LLC
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
  description = "Operator name used to identify the resource owner."
  type        = string
}

variable "environment" {
  description = "Assigned environment name to group related resources."
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
  description = "A GCP ssl certificate id. Example: projects/test-project/global/sslCertificates/dev. Used to terminate client-to-external-LB connections. Unused if frontend_certificate_map_id is specified."
  type        = string
  default     = ""
}

variable "frontend_certificate_map_id" {
  description = "A certificate manager certificate map resource id. Example: projects/test-project/locations/global/certificateMaps/wildcard-cert-map. Takes precedence over frontend_domain_ssl_certificate_id."
  type        = string
  default     = ""
}

variable "frontend_ssl_policy_id" {
  description = "A GCP ssl policy id. Example: projects/test-projects/global/sslPolicies/test-ssl-policy."
  type        = string
  default     = ""
}

variable "frontend_service_name" {
  type = string
}


variable "google_compute_backend_service_ids" {
  description = "a map with environment as key, the value is google_compute_backend_service_id"
  type        = map(string)
}

variable "traffic_weights" {
  description = "a map with environment as key, the value is traffic_weight between 0~1000"
  type        = map(number)
}
