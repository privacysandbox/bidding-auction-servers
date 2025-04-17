# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

variable "use_default_namespace" {
  type        = bool
  default     = false # Default to using the environment variable for k8s namespace
  description = "If true, use the 'default' namespace instead of the environment variable."
}

variable "seller_operators" {
  description = "List of sellers"
  type        = list(string)
  default     = ["seller1"]
}

variable "buyer_operators" {
  description = "List of buyers"
  type        = list(string)
  default     = ["buyer1"]
}

variable "seller_regions" {
  description = "List of Seller regions"
  type        = list(string)
  default     = ["eastus"]
}

variable "buyer_regions" {
  description = "List of Buyer regions"
  type        = list(string)
  default     = ["eastus"]
}

variable "seller_cert_email" {
  description = "Seller TLS Certificate email using Let's Encrypt"
  type        = string
}

variable "buyer_cert_email" {
  description = "Buyer TLS Certificate email using Let's Encrypt"
  type        = string
}

variable "seller_private_dns_zone_name" {
  description = "Seller Private DNS Zone Name"
  type        = string
}

variable "buyer_private_dns_zone_name" {
  description = "Buyer Private DNS Zone Name"
  type        = string
}

variable "subscription_id" {
  description = "Azure Subscription ID"
  type        = string
}

variable "seller_vn_replica_count" {
  description = "Seller Virtual Node replica count"
  type        = number
  default     = 1
}

variable "seller_vn_admission_controller_replica_count" {
  description = "Seller Virtual Node admission controller replica count"
  type        = number
  default     = 1
}

variable "seller_hpa_fe_min_replicas" {
  description = "Seller Frontend Service Horizontal Pod Autoscaler minimum replicas"
  type        = number
  default     = 1
}

variable "seller_hpa_fe_max_replicas" {
  description = "Seller Frontend Service Horizontal Pod Autoscaler maximum replicas"
  type        = number
  default     = 10
}

variable "seller_hpa_fe_avg_cpu_utilization" {
  description = "Seller Frontend Service Horizontal Pod Autoscaler target CPU utilization"
  type        = number
  default     = 75
}

variable "seller_hpa_min_replicas" {
  description = "Seller Backend Service Horizontal Pod Autoscaler minimum replicas"
  type        = number
  default     = 1
}

variable "seller_hpa_max_replicas" {
  description = "Seller Backend Service Horizontal Pod Autoscaler maximum replicas"
  type        = number
  default     = 10
}

variable "seller_hpa_avg_cpu_utilization" {
  description = "Seller Backend Service Horizontal Pod Autoscaler Average CPU utilization"
  type        = number
  default     = 75
}

variable "seller_dns_zone_name" {
  description = "Seller DNS zone name"
  type        = string
}

variable "seller_dns_zone_resource_group" {
  description = "Seller DNS zone resource group name"
  type        = string
}

variable "seller_fe_grpc_port" {
  description = "Seller Frontend gRPC port"
  type        = number
  default     = 50053
}

variable "seller_fe_http_port" {
  description = "Seller Frontend HTTP port"
  type        = number
  default     = 51052
}

variable "seller_grpc_port" {
  description = "Seller general gRPC port"
  type        = number
  default     = 50061
}

variable "seller_otel_grpc_port" {
  description = "Seller OTel gRPC port"
  type        = number
  default     = 4317
}

variable "seller_parc_image" {
  description = "Seller Parc Image"
  type        = string
}

variable "seller_parc_port" {
  description = "Seller Parc gRPC port"
  type        = number
  default     = 2000
}

variable "buyer_vn_replica_count" {
  description = "Buyer Virtual Node replica count"
  type        = number
  default     = 1
}

variable "buyer_vn_admission_controller_replica_count" {
  description = "Buyer Virtual Node admission controller replica count"
  type        = number
  default     = 1
}

variable "buyer_hpa_fe_min_replicas" {
  description = "Buyer Frontend Service Horizontal Pod Autoscaler minimum replicas"
  type        = number
  default     = 1
}

variable "buyer_hpa_fe_max_replicas" {
  description = "Buyer Frontend Service Horizontal Pod Autoscaler maximum replicas"
  type        = number
  default     = 10
}

variable "buyer_hpa_fe_avg_cpu_utilization" {
  description = "Buyer Frontend Service Horizontal Pod Autoscaler target CPU utilization"
  type        = number
  default     = 75
}

variable "buyer_hpa_min_replicas" {
  description = "Buyer Backend Service Horizontal Pod Autoscaler minimum replicas"
  type        = number
  default     = 1
}

variable "buyer_hpa_max_replicas" {
  description = "Buyer Backend Service Horizontal Pod Autoscaler maximum replicas"
  type        = number
  default     = 10
}

variable "buyer_hpa_avg_cpu_utilization" {
  description = "Buyer Backend Service Horizontal Pod Autoscaler Average CPU utilization"
  type        = number
  default     = 75
}

variable "buyer_dns_zone_name" {
  description = "Buyer DNS zone name"
  type        = string
}

variable "buyer_dns_zone_resource_group" {
  description = "Buyer DNS zone resource group name"
  type        = string
}

variable "buyer_fe_grpc_port" {
  description = "Buyer Frontend gRPC port"
  type        = number
  default     = 50051
}

variable "buyer_grpc_port" {
  description = "Buyer general gRPC port"
  type        = number
  default     = 50057
}

variable "buyer_otel_grpc_port" {
  description = "Buyer OTel gRPC port"
  type        = number
  default     = 4317
}

variable "buyer_parc_image" {
  description = "Buyer Parc Image"
  type        = string
}

variable "buyer_parc_port" {
  description = "Buyer Parc gRPC port"
  type        = number
  default     = 2000
}

variable "seller_regional_config" {
  description = "Configuration map for each seller region, the geo_tie_break_routing_priority is the second routing method used if the geographic latency is the same, the availability zones provide reliability within a region, and the vnet prefixes (which need to be different by region due to the lack of Kube-Proxy) provide the highest bit of the vnet cidr."
  type = map(object({
    geo_tie_break_routing_priority = number
    availability_zones             = list(string)
    vnet_prefix_high_bit           = number
  }))

  default = {
    "eastus" = {
      geo_tie_break_routing_priority = 1
      availability_zones             = ["eastus-1", "eastus-2", "eastus-3"]
      vnet_prefix_high_bit           = 10
    },
    "westus" = {
      geo_tie_break_routing_priority = 2
      availability_zones             = ["westus-1", "westus-2", "westus-3"]
      vnet_prefix_high_bit           = 11
    },
    "westeurope" = {
      geo_tie_break_routing_priority = 3
      availability_zones             = ["westeurope-1", "westeurope-2", "westeurope-3"]
      vnet_prefix_high_bit           = 12
    }
  }
}

variable "buyer_regional_config" {
  description = "Configuration map for each buyer region, the geo_tie_break_routing_priority is the second routing method used if the geographic latency is the same, the availability zones provide reliability within a region, and the vnet prefixes (which need to be different by region due to the lack of Kube-Proxy) provide the highest bit of the vnet cidr."
  type = map(object({
    geo_tie_break_routing_priority = number
    availability_zones             = list(string)
    vnet_prefix_high_bit           = number
  }))

  default = {
    "eastus" = {
      geo_tie_break_routing_priority = 1
      availability_zones             = ["eastus-1", "eastus-2", "eastus-3"]
      vnet_prefix_high_bit           = 10
    },
    "westus" = {
      geo_tie_break_routing_priority = 2
      availability_zones             = ["westus-1", "westus-2", "westus-3"]
      vnet_prefix_high_bit           = 11
    },
    "westeurope" = {
      geo_tie_break_routing_priority = 3
      availability_zones             = ["westeurope-1", "westeurope-2", "westeurope-3"]
      vnet_prefix_high_bit           = 12
    }
  }
}
