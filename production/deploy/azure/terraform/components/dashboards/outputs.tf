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


output "application_insights_otel_connection_string" {
  description = "Application Insights Connection String. OTel Collector uses to send data to Application Insights."
  value       = azurerm_application_insights.otel_dashboard.connection_string
}

output "application_insights_otel_instrumentation_key" {
  description = "Application Insights Instrumentation Key. OTel Collector uses to send data to Application Insights."
  value       = azurerm_application_insights.otel_dashboard.instrumentation_key
}

output "monitor_workspace_id" {
  description = "Azure Monitor Workspace ID"
  value       = azurerm_log_analytics_workspace.otel_dashboard.id
}
