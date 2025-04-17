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

# TLS Certificate Resources
# Create the User Assigned Managed-Identity
resource "azurerm_user_assigned_identity" "cert_manager_identity" {
  name                = "${var.operator}-${var.environment}-${var.region}-cert-manager-identity"
  resource_group_name = var.resource_group_name
  location            = var.region
}

data "azurerm_dns_zone" "dns_zone" {
  name                = var.dns_zone_name
  resource_group_name = var.dns_zone_resource_group
}

# DNS Zone Contributor Role Assignment for the Managed Identity
resource "azurerm_role_assignment" "dns_zone_contributor_role_assignment" {
  scope                = data.azurerm_dns_zone.dns_zone.id
  role_definition_name = "DNS Zone Contributor"
  principal_id         = azurerm_user_assigned_identity.cert_manager_identity.principal_id
}

# Create the Federated Credential for Workload Identity
resource "azurerm_federated_identity_credential" "cert_manager_identity_federated_credential" {
  name                = azurerm_user_assigned_identity.cert_manager_identity.name
  resource_group_name = var.resource_group_name
  parent_id           = azurerm_user_assigned_identity.cert_manager_identity.id
  issuer              = var.oidc_issuer_url
  subject             = "system:serviceaccount:cert-manager:cert-manager"
  audience            = ["api://AzureADTokenExchange"]
}

# Azure Kubernetes Service (AKS) Cluster with Virtual ACI Nodes Resources
# Virtual Node Identity Resource Group Contributor Role
resource "azurerm_role_assignment" "aks_identity_rg_contributor" {
  principal_id                     = var.principal_id
  role_definition_name             = "Contributor"
  scope                            = var.resource_group_id
  skip_service_principal_aad_check = true
}

# Virtual Node VNet Reader role
resource "azurerm_role_assignment" "aks_identity_vnet_reader" {
  principal_id                     = var.principal_id
  role_definition_name             = "Reader"
  scope                            = var.vnet_id
  skip_service_principal_aad_check = true
}

# Virtual Node Resource Group Contributor role
# The resource group needs contributor permission to create the virtual nodes within the AKS Cluster resource group.
resource "azurerm_role_assignment" "aks_kubeidentity_rg_contributor" {
  principal_id                     = var.kubelet_principal_id
  role_definition_name             = "Contributor"
  scope                            = var.resource_group_id
  skip_service_principal_aad_check = true
}

# Virtual Node AKS Cluster Resource Group Contributor role
# The Azure-managed default resource group needs contributor permission to create the virtual nodes within the AKS Cluster resource group.
resource "azurerm_role_assignment" "aks_kubeidentity_azure_managed_rg_contributor" {
  principal_id                     = var.kubelet_principal_id
  role_definition_name             = "Contributor"
  scope                            = var.node_resource_group_id
  skip_service_principal_aad_check = true
}

# Virtual Node User Assigned Identity
resource "azurerm_user_assigned_identity" "user_assigned_identity" {
  name                = "${var.operator}-${var.environment}-${var.region}-virtual-node-identity"
  location            = var.region
  resource_group_name = var.resource_group_name
}

# Application Gateway for Containers (AGfC) Resources
# Create the AGfC Managed Identity
resource "azurerm_user_assigned_identity" "agfc_identity" {
  name                = "${var.operator}-${var.environment}-${var.region}-azure-alb-identity"
  resource_group_name = var.resource_group_name
  location            = var.region
}

# Reader Role Assignment for the AGfC Managed Identity
resource "azurerm_role_assignment" "reader_role_assignment" {
  scope                = var.resource_group_id
  role_definition_name = "Reader"
  principal_id         = azurerm_user_assigned_identity.agfc_identity.principal_id
}

# Create the AGfC Federated Credential for Workload Identity
resource "azurerm_federated_identity_credential" "agfc_federated_credential" {
  name                = azurerm_user_assigned_identity.agfc_identity.name
  resource_group_name = var.resource_group_name
  parent_id           = azurerm_user_assigned_identity.agfc_identity.id
  issuer              = var.oidc_issuer_url
  subject             = "system:serviceaccount:azure-alb-system:alb-controller-sa"
  audience            = ["api://AzureADTokenExchange"]
}

# Deploy the ALB Controller using the `helm_release` resource for the AGfC
resource "helm_release" "alb_controller" {
  name       = "${var.operator}-${var.environment}-${var.region}-alb-controller"
  repository = "oci://mcr.microsoft.com/application-lb/charts"
  chart      = "alb-controller"
  version    = "1.3.7"

  set {
    name  = "albController.namespace"
    value = "azure-alb-system"
  }

  set {
    name  = "albController.podIdentity.clientID"
    value = azurerm_user_assigned_identity.agfc_identity.client_id
  }
}

# Role Assignment for App Gateway for Containers (AGfC) Configuration Manager
resource "azurerm_role_assignment" "appgw_config_manager" {
  scope                = var.resource_group_id
  role_definition_name = "AppGw for Containers Configuration Manager"
  principal_id         = azurerm_user_assigned_identity.agfc_identity.principal_id
  principal_type       = "ServicePrincipal"
}

# Role Assignment for AGfC Network Contributor on the Subnet
resource "azurerm_role_assignment" "subnet_network_contributor" {
  scope                = var.agfc_subnet_id
  role_definition_name = "Network Contributor"
  principal_id         = azurerm_user_assigned_identity.agfc_identity.principal_id
  principal_type       = "ServicePrincipal"
}

# Role connecting the Azure Container Registry (ACR) to the Azure Kubernetes Service (AKS)
resource "azurerm_role_assignment" "ACR-AKS" {
  principal_id                     = var.kubelet_principal_id
  role_definition_name             = "AcrPull"
  scope                            = var.acr_id
  skip_service_principal_aad_check = true
}
