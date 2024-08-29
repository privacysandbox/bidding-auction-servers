# Configure the Azure provider
terraform {
  required_providers {
    azurerm = {
      source  = "hashicorp/azurerm"
      version = "~> 4.0.1"
    }
  }

  required_version = ">= 1.1.0"
}

provider "azurerm" {
  features {}
}

resource "azurerm_resource_group" "rg" {
  name     = "terraform-test-rg"
  location = "centralindia"
}

resource "azurerm_virtual_network" "vnet" {
  name                = "terraform-test-vnet"
  address_space       = ["10.0.0.0/14"]
  location            = azurerm_resource_group.rg.location
  resource_group_name = azurerm_resource_group.rg.name

  depends_on = [
    azurerm_resource_group.rg,
  ]
}

resource "azurerm_subnet" "default" {
  name                 = "default"
  resource_group_name  = azurerm_resource_group.rg.name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = ["10.0.0.0/24"]

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_subnet" "aks" {
  name                 = "aks"
  resource_group_name  = azurerm_resource_group.rg.name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = ["10.1.0.0/16"]

  depends_on = [
    azurerm_virtual_network.vnet,
  ]
}

resource "azurerm_subnet" "cg" {
  name                 = "cg"
  resource_group_name  = azurerm_resource_group.rg.name
  virtual_network_name = azurerm_virtual_network.vnet.name
  address_prefixes     = ["10.2.0.0/16"]

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

resource "azurerm_kubernetes_cluster" "aks" {
  name                = "terraform-test-aks"
  location            = "Central India"
  resource_group_name = azurerm_resource_group.rg.name
  dns_prefix          = "terraform-test-aks-dns"
  kubernetes_version  = "1.28.12"

  network_profile {
    network_plugin     = "azure"
    network_policy     = "calico"
    network_data_plane = "azure"
    load_balancer_sku  = "standard"
    service_cidr       = "10.4.0.0/16"
    dns_service_ip     = "10.4.0.10"
    outbound_type      = "loadBalancer"
    service_cidrs      = ["10.4.0.0/16"]
  }

  default_node_pool {
    name           = "default"
    node_count     = 2
    vm_size        = "Standard_D4ds_v5"
    os_sku         = "Ubuntu"
    vnet_subnet_id = azurerm_subnet.aks.id
  }

  identity {
    type = "SystemAssigned"
  }



  depends_on = [
    azurerm_subnet.aks,
    azurerm_virtual_network.vnet,
  ]
}