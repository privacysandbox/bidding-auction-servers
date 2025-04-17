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

variable "namespace" {
  description = "Kubernetes Namespace"
  type        = string
}

variable "fe_service" {
  description = "Frontend Service Name"
  type        = string
}

variable "fe_grpc_port" {
  description = "Frontend gRPC Port"
  type        = number
}

variable "fe_http_port" {
  description = "Frontend Optional HTTP Port"
  type        = number
  default     = null
}

variable "service" {
  description = "Service Name"
  type        = string
}

variable "grpc_port" {
  description = "gRPC Port"
  type        = number
}
