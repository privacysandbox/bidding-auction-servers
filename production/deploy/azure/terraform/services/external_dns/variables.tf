variable "region" {
  description = "Azure region"
  type        = string
}

variable "private_domain_name" {
  description = "Azure Private DNS domain name"
  type        = string
  default     = "adsapi.microsoft"
}

variable "resource_group_id" {
  description = "Resource group ID"
  type        = string
}

variable "resource_group_name" {
  description = "Resource group name"
  type        = string
}

variable "vnet_id" {
  description = "Virtual network ID"
  type        = string
}

variable "vnet_name" {
  description = "Virtual network name"
  type        = string
}

variable "aks_cluster_name" {
  description = "Azure Kubernetes Service cluster name"
  type        = string
}

variable "aks_oidc_issuer_url" {
  description = "Azure Kubernetes Service OIDC issuer URL"
  type        = string
}

variable "kubernetes_namespace" {
  description = "External DNS namespace"
  type        = string
  default     = "external-dns"
}

variable "kubernetes_service_account" {
  description = "External DNS service account name"
  type        = string
  default     = "external-dns"
}

variable "tenant_id" {
  description = "Azure tenant ID"
  type        = string
}

variable "subscription_id" {
  description = "Azure subscription ID"
  type        = string
}


