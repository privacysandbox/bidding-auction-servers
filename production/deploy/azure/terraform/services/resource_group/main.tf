resource "azurerm_resource_group" "rg" {
  name     = "${var.operator}-${var.environment}-${var.frontend_service_name}-${var.region}-rg"
  location = var.region
}