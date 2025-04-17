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

# Traffic Manager Profile with Performance Routing Method
resource "azurerm_traffic_manager_profile" "global_load_balancer" {
  name                   = "${var.operator}-${var.environment}-traffic-manager-profile"
  resource_group_name    = var.resource_group_name
  traffic_routing_method = "Performance"
  dns_config {
    relative_name = "${var.operator}-${var.environment}-traffic-manager-profile"
    ttl           = 30
  }
  monitor_config {
    protocol                     = "HTTP"
    port                         = 80
    path                         = "/"
    interval_in_seconds          = 30
    timeout_in_seconds           = 10
    tolerated_number_of_failures = 3
  }
}

# CNAME Record connecting the Domain to the Traffic Manager Profile
resource "azurerm_dns_cname_record" "dns_cname_record" {
  name                = var.operator
  zone_name           = var.dns_zone_name
  resource_group_name = var.dns_zone_resource_group
  ttl                 = 60
  target_resource_id  = azurerm_traffic_manager_profile.global_load_balancer.id
}
