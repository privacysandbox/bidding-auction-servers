resource "azurerm_virtual_network" "vnet" {
  name                = "${var.operator}-${var.environment}-${var.frontend_service_name}-${var.region}-vnet"
  address_space       = [var.vnet_address_space]
  location            = var.region
  resource_group_name = var.resource_group_name
}

resource "azurerm_subnet" "default" {
  name                 = "default"
  resource_group_name  = var.resource_group_name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = [var.default_subnet_cidr]

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_subnet" "aks" {
  name                 = "aks"
  resource_group_name  = var.resource_group_name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = [var.aks_subnet_cidr]

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_subnet" "cg" {
  name                 = "cg"
  resource_group_name  = var.resource_group_name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = [var.cg_subnet_cidr]

  delegation {
    name = "delegation"
    service_delegation {
      name    = "Microsoft.ContainerInstance/containerGroups"
      actions = ["Microsoft.Network/virtualNetworks/subnets/action"]
    }
  }

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}