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

variable "resource_group_name" {
  description = "Resource Group Name"
  type        = string
}

variable "private_dns_zone_name" {
  description = "Private DNS Zone Name"
  type        = string
}

variable "collector_ips" {
  description = "External IP addresses of All Collectors"
  type        = list(string)
}

variable "parc_ips" {
  description = "External IP addresses of Parc Servers"
  type        = list(string)
}

variable "fe_service" {
  description = "Frontend Service Name. Ex: SFE or BFE"
  type        = string
}

variable "service" {
  description = "Service Name. Ex: Auction or Bidding"
  type        = string
}

variable "fe_service_ips" {
  description = "External IP addresses of frontend service"
  type        = list(string)
}

variable "service_ips" {
  description = "IP addresses of service"
  type        = list(string)
}
