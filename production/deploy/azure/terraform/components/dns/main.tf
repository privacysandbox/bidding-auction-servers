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

# Creates a Private DNS Zone
# Private dns zones are used because we are making all k8s services as
# type LoadBalancing instead of ClusterIP, which is not possible due to
# confidential ACI not supporting kube-proxy. This may be removed should
# we find a way for our ACI pods to conduct client-side load balancing.
resource "azurerm_private_dns_zone" "private-dns-zone" {
  name                = var.private_dns_zone_name
  resource_group_name = var.resource_group_name
}

# Creates a Private DNS A Record for the Frontend Services sharing the same operator. Ex: SFE or BFE
resource "azurerm_private_dns_a_record" "fe_service" {
  name                = var.fe_service
  zone_name           = var.private_dns_zone_name
  resource_group_name = var.resource_group_name
  ttl                 = "60"
  records             = var.fe_service_ips
}

# Creates a Private DNS A Record for the Backend Services sharing the same operator. Ex: Auction or Bidding
resource "azurerm_private_dns_a_record" "service" {
  name                = var.service
  zone_name           = var.private_dns_zone_name
  resource_group_name = var.resource_group_name
  ttl                 = "60"
  records             = var.service_ips
}

# Creates a Private DNS A Record for the OTEL Collectors sharing the same operator.
resource "azurerm_private_dns_a_record" "collector" {
  name                = "collector"
  zone_name           = var.private_dns_zone_name
  resource_group_name = var.resource_group_name
  ttl                 = "60"
  records             = var.collector_ips
}

# Creates a Private DNS A Record for the Parc servers sharing the same operator.
resource "azurerm_private_dns_a_record" "parc" {
  name                = "parc"
  zone_name           = var.private_dns_zone_name
  resource_group_name = var.resource_group_name
  ttl                 = "60"
  records             = var.parc_ips
}
