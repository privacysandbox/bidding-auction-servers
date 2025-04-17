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

output "fe_service_ip" {
  description = "Frontend Service IP Address"
  value       = data.kubernetes_service.fe_service.status[0].load_balancer[0].ingress[0].ip
}

output "service_ip" {
  description = "Service IP Address"
  value       = kubernetes_service.service.status[0].load_balancer[0].ingress[0].ip
}
