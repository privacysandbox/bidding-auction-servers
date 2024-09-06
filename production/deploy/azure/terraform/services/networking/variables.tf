variable "operator" {
  description = "Operator name used to identify the resource owner."
  type        = string
}

variable "environment" {
  description = "Assigned environment name to group related resources."
  type        = string
}

variable "frontend_service_name" {
  type = string
}

variable "region" {
  description = "Azure region"
  type        = string
}

variable "vnet_address_space" {
  description = "VNET address space"
  type        = string
  default     = "10.0.0.0/14"
}

variable "default_subnet_cidr" {
  description = "Default subnet CIDR"
  type        = string
  default     = "10.0.0.0/24"
}

variable "aks_subnet_cidr" {
  description = "AKS subnet CIDR"
  type        = string
  default     = "10.1.0.0/16"
}

variable "cg_subnet_cidr" {
  description = "Container groups subnet CIDR"
  type        = string
  default     = "10.2.0.0/16"
}

variable "resource_group_name" {
  description = "Resource group name"
  type        = string
}
