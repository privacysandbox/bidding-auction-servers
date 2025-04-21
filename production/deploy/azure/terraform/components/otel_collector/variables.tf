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

variable "region" {
  description = "Region. Ex: eastus, westus, etc."
  type        = string
}

variable "operator" {
  description = "Operator. Ex: buyer1, seller1, etc."
  type        = string
}

variable "environment" {
  description = "Environment. Ex: dev, prod, etc."
  type        = string
}

variable "resource_group_name" {
  description = "Resource Group Name"
  type        = string
}

variable "namespace" {
  description = "Namespace"
  type        = string
}

variable "otel_image" {
  description = "OTel Image Version"
  type        = string
  default     = "otel/opentelemetry-collector-contrib:latest"
}

variable "otel_grpc_port" {
  description = "OTel gRPC Port"
  type        = number
  default     = 4317
}

variable "otel_replicas" {
  description = "OTel Replicas"
  type        = number
  default     = 2
}

variable "application_insights_otel_connection_string" {
  description = "Application Insights Connection String. OTel Collector uses to send data to Application Insights."
  type        = string
}

variable "application_insights_otel_instrumentation_key" {
  description = "Application Insights Instrumentation Key. OTel Collector uses to send data to Application Insights."
  type        = string
}
