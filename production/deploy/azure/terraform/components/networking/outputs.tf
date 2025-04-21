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

output "agfc-subnet_id" {
  description = "Application Gateway for Containers Subnet ID"
  value       = azurerm_subnet.subnets["agfc"].id
}

output "aks-subnet_id" {
  description = "Azure Kubernetes Service Cluster Subnet ID"
  value       = azurerm_subnet.subnets["aks"].id
}

output "vnet_id" {
  description = "Virtual Network ID"
  value       = azurerm_virtual_network.aks-vnet.id
}

output "vnet_name" {
  description = "Virtual Network Name"
  value       = azurerm_virtual_network.aks-vnet.name
}
