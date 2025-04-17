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

# Resource to create a B&A Log Analytics Workspace
resource "azurerm_log_analytics_workspace" "otel_dashboard" {
  name                = "${var.operator}-${var.environment}-otel"
  location            = var.region
  resource_group_name = var.resource_group_name
}

# Resource to create a B&A Application Insights Dashboard
resource "azurerm_application_insights" "otel_dashboard" {
  name                = "${var.operator}-${var.environment}-otel"
  location            = var.region
  resource_group_name = var.resource_group_name
  application_type    = "other"
  workspace_id        = azurerm_log_analytics_workspace.otel_dashboard.id
}

resource "azurerm_portal_dashboard" "otel_dashboard" {
  name                = "${var.operator}-${var.environment}-dashboard"
  resource_group_name = var.resource_group_name
  location            = var.region
  dashboard_properties = templatefile("${path.module}/dashboard.tftpl",
    {
      application_insights_id = azurerm_application_insights.otel_dashboard.id,
      operator                = var.operator,
      environment             = var.environment,
      region                  = var.region
  })
}
