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

variable "region" {
  description = "Region. Ex: eastus, westus, etc."
  type        = string
}

variable "operator" {
  description = "Operator. Ex: buyer1, seller1, etc."
  type        = string
}

variable "environment" {
  description = "Environment. Ex: dev, prod, etc."
  type        = string
}

variable "resource_group_name" {
  description = "Resource Group Name"
  type        = string
}

variable "resource_group_id" {
  description = "Resource Group ID"
  type        = string
}

variable "vnet_id" {
  description = "Virtual Network ID"
  type        = string
}

variable "aks_subnet_id" {
  description = "Azure Kubernetes Cluster Subnet ID"
  type        = string
}

variable "monitor_workspace_id" {
  description = "Azure Monitor Workspace ID"
  type        = string
}

# Should NOT overlap with any of the subnet cidrs'. See: ./production/deploy/azure/terraform/components/networking/main.tf
variable "aks_service_cidr" {
  description = "Azure Kubernetes Service (AKS) Cluster Service CIDR"
  type        = string
  default     = "10.4.0.0/16"
}

variable "aks_dns_service_ip" {
  description = "Azure Kubernetes Service (AKS) Cluster DNS Service IP"
  type        = string
  default     = "10.4.0.10"
}

variable "autoscaling_max_node_count" {
  description = "Azure Kubernetes Service (AKS) Autoscaling Node Max"
  type        = number
  default     = 100
}

variable "namespace" {
  description = "Kubernetes Namespace"
  type        = string
}
