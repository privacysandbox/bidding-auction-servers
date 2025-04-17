/**
 * Copyright 2023 Google LLC
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

variable "fe_service" {
  description = "Front-End Service Name"
  type        = string
}

variable "service" {
  description = "Service Name"
  type        = string
}

variable "namespace" {
  description = "Namespace"
  type        = string
}

variable "hpa_fe_min_replicas" {
  description = "Frontend Horizontal Pod Autoscaling Min Replicas"
  type        = number
  default     = 1
}

variable "hpa_fe_max_replicas" {
  description = "Frontend Horizontal Pod Autoscaling Max Replicas"
  type        = number
  default     = 10
}

variable "hpa_fe_avg_cpu_utilization" {
  description = "Frontend Horizontal Pod Autoscaling Average CPU Utilization "
  type        = number
  default     = 75
}

variable "hpa_min_replicas" {
  description = "Horizontal Pod Autoscaling Min Replicas"
  type        = number
  default     = 1
}

variable "hpa_max_replicas" {
  description = "Horizontal Pod Autoscaling Max Replicas"
  type        = number
  default     = 10
}

variable "hpa_avg_cpu_utilization" {
  description = "Horizontal Pod Autoscaling Average CPU Utilization "
  type        = number
  default     = 75
}

variable "frontend_deployment_name" {
  description = "Frontend Deployment Name"
  type        = string
}

variable "backend_deployment_name" {
  description = "Backend Deployment Name"
  type        = string
}
