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

output "parc_ip" {
  description = "Parc Service IP Address"
  value       = data.kubernetes_service.parc_service.status[0].load_balancer[0].ingress[0].ip
}

output "parc_config_map_hash" {
  description = "Parc ConfigMap Hash"
  value       = sha256(jsonencode(kubernetes_config_map.parc-parameters.data))
}
