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

variable "resource_group_name" {
  description = "Resource group name"
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

variable "subnet_id" {
  description = "AGfC Subnet ID"
  type        = string
}

variable "traffic_manager_profile_id" {
  description = "Traffic Manager Resource ID (Global Load Balancer ID)"
  type        = string
}

variable "geo_tie_break_routing_priority" {
  description = "Secondary Priority when Geographic Performance Routing is the same."
  type        = string
}
