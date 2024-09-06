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

variable "resource_group_id" {
  description = "Resource group ID"
  type        = string
}

variable "resource_group_name" {
  description = "Resource group name"
  type        = string
}

variable "kubernetes_version" {
  description = "Kubernetes version"
  type        = string
  default     = "1.28.12"
}

variable "service_cidr" {
  description = "Service CIDR"
  type        = string
  default     = "10.4.0.0/16"
}

variable "dns_service_ip" {
  description = "DNS service IP"
  type        = string
  default     = "10.4.0.10"
}

variable "subnet_id" {
  description = "Subnet ID"
  type        = string
}

variable "virtual_network_id" {
  description = "Virtual network ID"
  type        = string
}

