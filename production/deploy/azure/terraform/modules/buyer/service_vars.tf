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

variable "environment" {
  description = "Environment. Ex: dev, prod, etc."
  type        = string
  validation {
    condition     = length(var.environment) <= 10
    error_message = "Due to current naming scheme limitations, environment must not be longer than 10."
  }
}

variable "operator" {
  description = "Operator. Ex: buyer1, seller1, etc"
  type        = string
}

variable "subscription_id" {
  description = "Azure Subscription ID"
  type        = string
}

variable "tenant_id" {
  description = "Azure Tenant ID"
  type        = string
}

variable "cert_email" {
  description = "Email used for Let's Encrypt Certificate"
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

variable "global_load_balancer_id" {
  description = "Global Load Balancer (Traffic Manager) Resource ID"
  type        = string
}

variable "geo_tie_break_routing_priority" {
  description = "Secondary Priority when Geographic Performance Routing is the same."
  type        = string
}

variable "private_dns_zone_name" {
  description = "Domain Name"
  type        = string
}

variable "zone" {
  description = "Zone Name"
  type        = string
}

variable "namespace" {
  description = "Namespace"
  type        = string
}

variable "vnet_cidr" {
  description = "Virtual Network CIDR"
  type        = string
}

variable "default_subnet_cidr" {
  description = "Default Subnet CIDR"
  type        = string
}

variable "aks_subnet_cidr" {
  description = "Azure Kubernetes Service Subnet CIDR"
  type        = string
}

variable "cg_subnet_cidr" {
  description = "Container Group Subnet CIDR, used for Virtual Nodes"
  type        = string
}

variable "agfc_subnet_cidr" {
  description = "App Gateway for Containers Subnet CIDR"
  type        = string
}

variable "aks_service_cidr" {
  description = "Azure Kubernetes Service CIDR"
  type        = string
}

variable "aks_dns_service_cidr" {
  description = "Azure Kubernetes Service DNS Service CIDR"
  type        = string
}

variable "vn_replica_count" {
  description = "Virtual Node Replica Count"
  type        = number
}

variable "vn_admission_controller_replica_count" {
  description = "Virtual Node Admission Controller Replica Count"
  type        = number
}

variable "hpa_fe_min_replicas" {
  description = "Frontend Service Horizontal Pod Autoscaling Minimum Replicas"
  type        = number
}

variable "hpa_fe_max_replicas" {
  description = "Frontend Service Horizontal Pod Autoscaling Maximum Replicas"
  type        = number
}

variable "hpa_fe_avg_cpu_utilization" {
  description = "Frontend Service Horizontal Pod Autoscaling Average CPU Utilization"
  type        = number
}

variable "hpa_min_replicas" {
  description = "Horizontal Pod Autoscaling Minimum Replicas"
  type        = number
}

variable "hpa_max_replicas" {
  description = "Horizontal Pod Autoscaling Maximum Replicas"
  type        = number
}

variable "hpa_avg_cpu_utilization" {
  description = "Horizontal Pod Autoscaling Average CPU Utilization"
  type        = number
}

variable "dns_zone_name" {
  description = "DNS Zone Name"
  type        = string
}

variable "dns_zone_resource_group" {
  description = "DNS Resource Group"
  type        = string
}

variable "fe_grpc_port" {
  description = "Frontend gRPC port"
  type        = number
}

variable "grpc_port" {
  description = "gRPC port"
  type        = number
}

variable "otel_grpc_port" {
  description = "OTel gRPC Port"
  type        = number
}

variable "application_insights_otel_connection_string" {
  description = "Application Insights OTel Connection String"
  type        = string
}

variable "application_insights_otel_instrumentation_key" {
  description = "Application Insights OTel Instrumentation Key"
  type        = string
}

variable "monitor_workspace_id" {
  description = "Azure OTel Monitor Workspace ID"
  type        = string
}

variable "parc_image" {
  description = "Parc Server Image"
  type        = string
}

variable "parc_port" {
  description = "Parc gRPC Port"
  type        = number
  default     = 2000
}
