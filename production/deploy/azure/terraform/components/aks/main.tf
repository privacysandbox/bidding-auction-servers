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

# Creates a Managed Kubernetes Cluster with Virtual ACI Nodes, a System-Assigned Managed Identity, Node Autoscaling, connection to Azure Key Vault, and Azure Monitor.
resource "azurerm_kubernetes_cluster" "aks" {
  name                      = "${var.operator}-${var.environment}-${var.region}-aks-cluster"
  location                  = var.region
  resource_group_name       = var.resource_group_name
  dns_prefix                = "${var.operator}-${var.environment}-${var.region}"
  oidc_issuer_enabled       = true
  workload_identity_enabled = true
  automatic_upgrade_channel = "patch"

  default_node_pool {
    name                 = "default"
    node_count           = 2
    vm_size              = "Standard_D4ds_v4"
    vnet_subnet_id       = var.aks_subnet_id
    auto_scaling_enabled = true
    min_count            = 2
    max_count            = var.autoscaling_max_node_count
  }

  identity {
    type = "SystemAssigned"
  }

  network_profile {
    network_plugin     = "azure"
    network_policy     = "calico"
    network_data_plane = "azure"
    load_balancer_sku  = "standard"
    service_cidr       = var.aks_service_cidr
    dns_service_ip     = var.aks_dns_service_ip
    outbound_type      = "loadBalancer"
    service_cidrs      = [var.aks_service_cidr]
  }

  key_vault_secrets_provider {
    secret_rotation_enabled = false
  }

  oms_agent {
    msi_auth_for_monitoring_enabled = true
    log_analytics_workspace_id      = var.monitor_workspace_id
  }
}

resource "kubernetes_namespace" "namespace" {
  count = var.namespace == "default" ? 0 : 1
  metadata {
    name = var.namespace
  }
}
