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

# Application Gateway for Containers (AGfC)
resource "azurerm_application_load_balancer" "agfc" {
  name                = "${var.operator}-${var.environment}-${var.region}-agfc"
  resource_group_name = var.resource_group_name
  location            = var.region
}

# AGfC Frontend
resource "azurerm_application_load_balancer_frontend" "agfc_fe" {
  name                         = "${var.operator}-${var.environment}-${var.region}-agfc_fe"
  application_load_balancer_id = azurerm_application_load_balancer.agfc.id
}

# AGfC Backend (Subnet Association)
resource "azurerm_application_load_balancer_subnet_association" "agfc_sa" {
  name                         = "${var.operator}-${var.environment}-${var.region}-agfc_sa"
  application_load_balancer_id = azurerm_application_load_balancer.agfc.id
  subnet_id                    = var.subnet_id
}

# Traffic Manager Endpoint connected to AGfC Frontend
resource "azurerm_traffic_manager_external_endpoint" "ex_ep" {
  name                 = "${var.operator}-${var.environment}-${var.region}-external_endpoint"
  profile_id           = var.traffic_manager_profile_id
  always_serve_enabled = true
  endpoint_location    = var.region
  priority             = var.geo_tie_break_routing_priority
  target               = azurerm_application_load_balancer_frontend.agfc_fe.fully_qualified_domain_name
}
