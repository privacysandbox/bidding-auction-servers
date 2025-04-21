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

# Creates a Virtual Network for resources in resource group
resource "azurerm_virtual_network" "aks-vnet" {
  name                = "${var.operator}-${var.environment}-${var.region}-vnet"
  resource_group_name = var.resource_group_name
  address_space       = [var.vnet_cidr]
  location            = var.region
}

# Creates a Private DNS Zone Link to the Virtual Network
resource "azurerm_private_dns_zone_virtual_network_link" "vnet-link" {
  name                  = "${azurerm_virtual_network.aks-vnet.name}-link"
  resource_group_name   = var.resource_group_name
  private_dns_zone_name = var.private_dns_zone_name
  virtual_network_id    = azurerm_virtual_network.aks-vnet.id
}

# Creates subnets for aks cluster, virtual nodes, agfc, and a default subnet
resource "azurerm_subnet" "subnets" {
  for_each = {
    default = {
      name             = "default"
      address_prefixes = [var.default_subnet_cidr]
      delegation       = null
    }
    aks = {
      name             = "aks"
      address_prefixes = [var.aks_subnet_cidr]
      delegation       = null
    }
    cg = {
      name             = "cg"
      address_prefixes = [var.cg_subnet_cidr]
      delegation = {
        name = "cg-delegation"
        service_delegation = {
          name    = "Microsoft.ContainerInstance/containerGroups"
          actions = ["Microsoft.Network/virtualNetworks/subnets/action"]
        }
      }
    }
    agfc = {
      name             = "${var.operator}-${var.environment}-${var.region}-agfc-subnet"
      address_prefixes = [var.agfc_subnet_cidr]
      delegation = {
        name = "agfc-delegation"
        service_delegation = {
          name    = "Microsoft.ServiceNetworking/trafficControllers"
          actions = ["Microsoft.Network/virtualNetworks/subnets/join/action"]
        }
      }
    }
  }

  name                 = each.value.name
  resource_group_name  = var.resource_group_name
  virtual_network_name = azurerm_virtual_network.aks-vnet.name
  address_prefixes     = each.value.address_prefixes

  dynamic "delegation" {
    for_each = each.value.delegation != null ? [each.value.delegation] : []
    content {
      name = delegation.value.name
      service_delegation {
        name    = delegation.value.service_delegation.name
        actions = delegation.value.service_delegation.actions
      }
    }
  }

  timeouts {
    create = "5m"
    update = "5m"
    delete = "5m"
  }
}
