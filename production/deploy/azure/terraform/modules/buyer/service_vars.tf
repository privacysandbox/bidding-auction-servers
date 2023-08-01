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

variable "regions" {
  description = "Regions to deploy to."
  type        = set(string)
}

variable "azure_project_id" {
  description = "GCP project id."
  type        = string
}

variable "service_account_email" {
  description = "Service account email address"
  type        = string
}

variable "bidding_image" {
  description = "URL to bidding service TEE Docker image"
  type        = string
}

variable "buyer_frontend_image" {
  description = "URL to bfe service TEE Docker image"
  type        = string
}

variable "frontend_domain_name" {
  description = "Google Cloud Domain name for global external LB"
  type        = string
}

variable "frontend_dns_zone" {
  description = "Google Cloud DNS zone name for the frontend domain"
  type        = string
}

variable "frontend_domain_ssl_certificate_id" {
  description = "A GCP ssl certificate id. Example: projects/bas-dev-383721/global/sslCertificates/dev"
  type        = string
}

variable "min_replicas_per_service_region" {
  description = "Minimum amount of replicas per each service region (a single managed instance group)."
  type        = number
}

variable "max_replicas_per_service_region" {
  description = "Maximum amount of replicas per each service region (a single managed instance group)."
  type        = number
}
variable "vm_startup_delay_seconds" {
  description = "The time it takes to get a service up and responding to heartbeats (in seconds)."
  type        = number
}

variable "cpu_utilization_percent" {
  description = "CPU utilization percentage across an instance group required for autoscaler to add instances."
  type        = number
}

variable "runtime_flags" {
  type        = map(string)
  description = "Buyer runtime flags. Must exactly match flags specified in <project root>/services/(bidding_service|buyer_frontend_service)/runtime_flags.h"
}

variable "collector_service_port" {
  description = "The grpc port that receives traffic destined for the OpenTelemetry collector."
  type        = number
}

variable "bfe_machine_type" {
  description = "Machine type for the Buyer Frontend Service. Must be compatible with confidential compute."
  type        = string
}

variable "bidding_machine_type" {
  description = "Machine type for the Bidding Service. Must be compatible with confidential compute."
  type        = string
}

variable "collector_machine_type" {
  description = "Machine type for the collector service."
  type        = string
}
