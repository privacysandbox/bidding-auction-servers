# Variables related to environment configuration.
variable "environment" {
  description = "Assigned environment name to group related resources."
  type        = string
  validation {
    condition     = length(var.environment) <= 10
    error_message = "Due to current naming scheme limitations, environment must not be longer than 10."
  }
}

variable "operator" {
  description = "Operator name used to identify the resource owner."
  type        = string
}

variable "region" {
  description = "Azure region"
  type        = string
}

variable "private_domain_name" {
  description = "Azure Private DNS domain name"
  type        = string
  default     = "adsapi.microsoft"
}

variable "tenant_id" {
  description = "Azure tenant ID"
  type        = string
}

variable "subscription_id" {
  description = "Azure subscription ID"
  type        = string
}

variable "externaldns_kubernetes_namespace" {
  description = "External DNS namespace"
  type        = string
  default     = "external-dns"
}

variable "externaldns_kubernetes_service_account" {
  description = "External DNS service account name"
  type        = string
  default     = "external-dns"
}


