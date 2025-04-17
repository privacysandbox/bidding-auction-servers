# Copyright 2024 Google LLC
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

output "google_compute_backend_service_id" {
  value = module.load_balancing.google_compute_backend_service_id
}

output "frontend_address" {
  value = module.networking.frontend_address
}

output "collector_endpoint" {
  value = var.runtime_flags["COLLECTOR_ENDPOINT"]
}

output "mesh_id" {
  value = module.networking.mesh.id
}

output "vpc_id" {
  value = module.networking.network_id
}

output "regions" {
  value = keys(var.region_config)
}

output "gcp_project_id" {
  value = var.gcp_project_id
}

output "environment" {
  value = var.environment
}

output "operator" {
  value = var.operator
}
