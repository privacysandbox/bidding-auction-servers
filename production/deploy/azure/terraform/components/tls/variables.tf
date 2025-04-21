/**
 * Copyright 2025 Google LLC
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

variable "namespace" {
  description = "Namespace"
  type        = string
}

variable "subscription_id" {
  description = "Azure Subscription ID"
  type        = string
}

variable "cert_email" {
  description = "Email Associated with the Let's Encrypt Cluster Issuer"
  type        = string
}

variable "user_assigned_client_id" {
  description = "User Assigned Client ID"
  type        = string
}

variable "agfc_resource_id" {
  description = "App Gateway for Containers (Resource) ID"
  type        = string
}

variable "agfc_fe_name" {
  description = "App Gateway for Containers (Frontend) Name"
  type        = string
}

variable "fe_grpc_port" {
  description = "Frontend GRPC Port"
  type        = string
}

variable "fe_service" {
  description = "Frontend Service Name"
  type        = string
}

variable "dns_zone_name" {
  description = "DNS Zone Name"
  type        = string
}
