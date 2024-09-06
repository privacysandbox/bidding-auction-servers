# Portions Copyright (c) Microsoft Corporation
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

# Configure the Azure provider
terraform {
  required_providers {
    azurerm = {
      source  = "hashicorp/azurerm"
      version = "~> 4.0.1"
    }
  }

  required_version = ">= 1.1.0"
}

provider "azuread" {
  use_cli = true
}

provider "azurerm" {
  features {}
}

resource "azurerm_resource_group" "rg" {
  name     = "takuro-test-rg"
  location = "centralindia"
}

resource "azurerm_virtual_network" "vnet" {
  name                = "takuro-test-vnet"
  address_space       = ["10.0.0.0/14"]
  location            = azurerm_resource_group.rg.location
  resource_group_name = azurerm_resource_group.rg.name

  depends_on = [
    azurerm_resource_group.rg,
  ]
}

resource "azurerm_subnet" "default" {
  name                 = "default"
  resource_group_name  = azurerm_resource_group.rg.name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = ["10.0.0.0/24"]

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_subnet" "aks" {
  name                 = "aks"
  resource_group_name  = azurerm_resource_group.rg.name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = ["10.1.0.0/16"]

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_subnet" "cg" {
  name                 = "cg"
  resource_group_name  = azurerm_resource_group.rg.name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = ["10.2.0.0/16"]

  delegation {
    name = "delegation"
    service_delegation {
      name    = "Microsoft.ContainerInstance/containerGroups"
      actions = ["Microsoft.Network/virtualNetworks/subnets/action"]
    }
  }

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_kubernetes_cluster" "aks" {
  name                      = "takuro-test-aks"
  location                  = "Central India"
  resource_group_name       = azurerm_resource_group.rg.name
  dns_prefix                = "takuro-test-aks-dns"
  kubernetes_version        = "1.28.12"
  workload_identity_enabled = true
  oidc_issuer_enabled       = true
  automatic_upgrade_channel = "patch"

  network_profile {
    network_plugin     = "azure"
    network_policy     = "calico"
    network_data_plane = "azure"
    load_balancer_sku  = "standard"
    service_cidr       = "10.4.0.0/16"
    dns_service_ip     = "10.4.0.10"
    outbound_type      = "loadBalancer"
    service_cidrs      = ["10.4.0.0/16"]
  }

  default_node_pool {
    name           = "default"
    node_count     = 2
    vm_size        = "Standard_D4ds_v5"
    os_sku         = "Ubuntu"
    vnet_subnet_id = azurerm_subnet.aks.id
  }

  identity {
    type = "SystemAssigned"
  }

  depends_on = [
    azurerm_subnet.aks,
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_role_assignment" "aks_identity_rg_contributor" {
  principal_id                     = azurerm_kubernetes_cluster.aks.identity[0].principal_id
  role_definition_name             = "Contributor"
  scope                            = azurerm_resource_group.rg.id
  skip_service_principal_aad_check = true

  depends_on = [
    azurerm_kubernetes_cluster.aks,
  ]
}

resource "azurerm_role_assignment" "aks_identity_vnet_reader" {
  principal_id                     = azurerm_kubernetes_cluster.aks.identity[0].principal_id
  role_definition_name             = "Reader"
  scope                            = azurerm_virtual_network.vnet.id
  skip_service_principal_aad_check = true

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_role_assignment" "aks_kubeidentity_rg_contributor" {
  principal_id                     = azurerm_kubernetes_cluster.aks.kubelet_identity[0].object_id
  role_definition_name             = "Contributor"
  scope                            = azurerm_resource_group.rg.id
  skip_service_principal_aad_check = true

  depends_on = [
    azurerm_kubernetes_cluster.aks,
  ]
}

resource "azurerm_role_assignment" "aks_kubeidentity_mcrg_contributor" {
  principal_id                     = azurerm_kubernetes_cluster.aks.kubelet_identity[0].object_id
  role_definition_name             = "Contributor"
  scope                            = azurerm_kubernetes_cluster.aks.node_resource_group_id
  skip_service_principal_aad_check = true

  depends_on = [
    azurerm_kubernetes_cluster.aks,
  ]
}

resource "azurerm_private_dns_zone" "this" {
  name                = "adsapi.microsoft"
  resource_group_name = azurerm_resource_group.rg.name
}

resource "azurerm_private_dns_zone_virtual_network_link" "this" {
  name                  = "takuro-test-vnet-link"
  resource_group_name   = azurerm_resource_group.rg.name
  private_dns_zone_name = azurerm_private_dns_zone.this.name
  virtual_network_id    = azurerm_virtual_network.vnet.id

  depends_on = [
    azurerm_private_dns_zone.this,
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_user_assigned_identity" "externaldns" {
  name                = "takuro-externaldns-identity"
  location            = azurerm_resource_group.rg.location
  resource_group_name = azurerm_resource_group.rg.name

}

resource "azurerm_federated_identity_credential" "this" {
  name                = "${azurerm_kubernetes_cluster.aks.name}-ServiceAccount-externaldns-external-dns"
  resource_group_name = azurerm_resource_group.rg.name
  audience            = ["api://AzureADTokenExchange"]
  issuer              = azurerm_kubernetes_cluster.aks.oidc_issuer_url
  parent_id           = azurerm_user_assigned_identity.externaldns.id
  subject             = "system:serviceaccount:external-dns:external-dns"

  depends_on = [
    azurerm_user_assigned_identity.externaldns,
    azurerm_kubernetes_cluster.aks,
  ]
}

resource "azurerm_role_assignment" "reader" {
  principal_id                     = azurerm_user_assigned_identity.externaldns.principal_id
  role_definition_name             = "Reader"
  scope                            = azurerm_resource_group.rg.id
  skip_service_principal_aad_check = true

  depends_on = [
    azurerm_resource_group.rg,
    azurerm_user_assigned_identity.externaldns,
  ]
}

resource "azurerm_role_assignment" "private_dns_zone_contributor" {
  principal_id                     = azurerm_user_assigned_identity.externaldns.principal_id
  role_definition_name             = "Private DNS Zone Contributor"
  scope                            = azurerm_private_dns_zone.this.id
  skip_service_principal_aad_check = true

  depends_on = [
    azurerm_private_dns_zone.this,
    azurerm_user_assigned_identity.externaldns,
  ]
}

output "externaldns_identity_client_id" {
  description = "The client ID of the created managed identity to use for the annotation 'azure.workload.identity/client-id' on your service account"
  value       = azurerm_user_assigned_identity.externaldns.client_id
}
