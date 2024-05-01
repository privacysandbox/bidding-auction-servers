/**
 * Copyright 2022 Google LLC
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

variable "operator" {
  description = "Assigned name of an operator in Bidding & Auction system, i.e. seller1, buyer1, buyer2."
  type        = string
}

variable "environment" {
  description = "Assigned environment name to group related resources."
  type        = string
}

variable "autoscaling_desired_capacity" {
  type = number
}

variable "autoscaling_max_size" {
  type = number
}

variable "autoscaling_min_size" {
  type    = number
  default = 0
}

variable "autoscaling_subnet_ids" {
  type = list(string)
}

variable "instance_ami_id" {
  type = string
}

variable "instance_type" {
  type = string
}

variable "instance_security_group_id" {
  type = string
}

variable "instance_profile_arn" {
  description = "Profile to attach to instances when they are launched."
  type        = string
}

variable "target_group_arns" {
  type = list(string)
}

variable "enclave_memory_mib" {
  description = "Amount of memory to allocate to the enclave."
  type        = number
}

variable "enclave_cpu_count" {
  description = "The number of vcpus to allocate to the enclave."
  type        = number
}

variable "service" {
  description = "One of: bidding, auction, buyer-frontend, seller-frontend"
  type        = string
}

variable "enclave_debug_mode" {
  description = "If true, starts the Nitro enclave with --debug-mode."
  type        = bool
  default     = false
}

variable "enclave_log_path" {
  description = "Absolute path to where nitro enclave logs will be written. Only used if enclave_debug_mode = true."
  type        = string
  default     = "/output.log"
}
