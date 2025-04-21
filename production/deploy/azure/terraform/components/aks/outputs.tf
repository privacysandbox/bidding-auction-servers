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

output "name" {
  description = "Name of the Azure Kubernetes Cluster"
  value       = azurerm_kubernetes_cluster.aks.name
}

output "aks_fqdn" {
  description = "FQDN of the Azure Kubernetes Cluster"
  value       = azurerm_kubernetes_cluster.aks.fqdn
}

output "aks_id" {
  description = "Resource ID of the Azure Kubernetes Cluster"
  value       = azurerm_kubernetes_cluster.aks.id
}

output "oidc_issuer_url" {
  description = "OpenID Connect Issuer URL of the Azure Kubernetes Cluster"
  value       = azurerm_kubernetes_cluster.aks.oidc_issuer_url
}

output "principal_id" {
  description = "Principal ID of the Azure Kubernetes Cluster System Assigned Managed Identity"
  value       = azurerm_kubernetes_cluster.aks.identity[0].principal_id
}

output "node_resource_group_id" {
  description = "Resource Group ID of the AKS Node Resource Group, different resource group than the AKS Cluster, starts with MC"
  value       = azurerm_kubernetes_cluster.aks.node_resource_group_id
}

output "kubelet_principal_id" {
  description = "Kubelet Principal ID"
  value       = azurerm_kubernetes_cluster.aks.kubelet_identity[0].object_id
}

output "kube_config_host" {
  description = "Kube Config Host, used for provider configuration"
  value       = azurerm_kubernetes_cluster.aks.kube_config[0].host
}

output "kube_config_client_certificate" {
  description = "Kube Config Client Certificate, used for provider configuration"
  value       = azurerm_kubernetes_cluster.aks.kube_config.0.client_certificate
}

output "kube_config_client_key" {
  description = "Kube Config Client Key, used for provider configuration"
  value       = azurerm_kubernetes_cluster.aks.kube_config.0.client_key
}

output "kube_config_cluster_ca_certificate" {
  description = "Kube Config Cluster CA Certificate, used for provider configuration"
  value       = azurerm_kubernetes_cluster.aks.kube_config.0.cluster_ca_certificate
}
