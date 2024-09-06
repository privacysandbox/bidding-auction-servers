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