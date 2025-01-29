/**
 * Copyright 2022 Google LLC
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

################################################################################
# Setup VPC, and networking for private subnets and public subnets.
################################################################################

# Create the VPC where server instances will be launched.
resource "aws_vpc" "vpc" {
  cidr_block = var.vpc_cidr_block
  // Our networking infra is dual-stack, we use both an IPv4 and an IPv6 CIDR block.
  assign_generated_ipv6_cidr_block = true
  enable_dns_support               = true
  enable_dns_hostnames             = true

  tags = {
    Name        = "${var.operator}-${var.environment}-vpc"
    operator    = var.operator
    environment = var.environment
  }
}

# Get information about available AZs.
data "aws_availability_zones" "azs" {
  state = "available"
}

# Create public subnets used to connect to instances in private subnets.
resource "aws_subnet" "public_subnet" {
  count                   = length(data.aws_availability_zones.azs.names)
  cidr_block              = cidrsubnet(aws_vpc.vpc.cidr_block, 4, count.index)
  ipv6_cidr_block         = cidrsubnet(aws_vpc.vpc.ipv6_cidr_block, 4, count.index)
  vpc_id                  = aws_vpc.vpc.id
  availability_zone       = data.aws_availability_zones.azs.names[count.index]
  map_public_ip_on_launch = true
  // If the operator wishes to enable IPv6 ingress, the operator will have to run the ELBs in dual-stack mode, and may have to modify this setting to true.
  assign_ipv6_address_on_creation = false

  tags = {
    Name        = "${var.operator}-${var.environment}-public-subnet${count.index}"
    operator    = var.operator
    environment = var.environment
  }
}

# Create private subnets where instances will be launched.
resource "aws_subnet" "private_subnet" {
  count                           = length(data.aws_availability_zones.azs.names)
  cidr_block                      = cidrsubnet(aws_vpc.vpc.cidr_block, 4, 15 - count.index)
  ipv6_cidr_block                 = cidrsubnet(aws_vpc.vpc.ipv6_cidr_block, 4, 15 - count.index)
  vpc_id                          = aws_vpc.vpc.id
  availability_zone               = data.aws_availability_zones.azs.names[count.index]
  map_public_ip_on_launch         = false
  assign_ipv6_address_on_creation = true

  tags = {
    Name        = "${var.operator}-${var.environment}-private-subnet${count.index}"
    operator    = var.operator
    environment = var.environment
  }
}

# Create networking components for public subnets.
resource "aws_internet_gateway" "igw" {
  vpc_id = aws_vpc.vpc.id

  tags = {
    Name        = "${var.operator}-${var.environment}-igw"
    operator    = var.operator
    environment = var.environment
  }
}

resource "aws_route_table" "public_rt" {
  vpc_id = aws_vpc.vpc.id

  tags = {
    Name        = "${var.operator}-${var.environment}-public-rt"
    operator    = var.operator
    environment = var.environment
  }
}

resource "aws_route" "public_route" {
  route_table_id         = aws_route_table.public_rt.id
  gateway_id             = aws_internet_gateway.igw.id
  destination_cidr_block = "0.0.0.0/0"

  depends_on = [
    aws_internet_gateway.igw
  ]
}

resource "aws_route" "public_route_ipv6" {
  route_table_id              = aws_route_table.public_rt.id
  gateway_id                  = aws_internet_gateway.igw.id
  destination_ipv6_cidr_block = "::/0"
}

resource "aws_route_table_association" "public_rt_assoc" {
  count          = length(aws_subnet.public_subnet)
  subnet_id      = aws_subnet.public_subnet[count.index].id
  route_table_id = aws_route_table.public_rt.id
}

# Create private route tables required for gateway endpoints.
resource "aws_route_table" "private_rt" {
  count  = length(aws_subnet.private_subnet)
  vpc_id = aws_vpc.vpc.id

  tags = {
    Name        = "${var.operator}-${var.environment}-private-rt${count.index}"
    operator    = var.operator
    environment = var.environment
  }
}

resource "aws_route_table_association" "private_rt_assoc" {
  count          = length(aws_subnet.private_subnet)
  route_table_id = aws_route_table.private_rt[count.index].id
  subnet_id      = aws_subnet.private_subnet[count.index].id
}

resource "aws_eip" "nat_gateway" {
  count = length(aws_subnet.private_subnet)
  vpc   = true
  tags = {
    Name        = "${var.operator}-${var.environment}-nat-gateway-eip-${count.index}"
    operator    = var.operator
    environment = var.environment
  }

  depends_on = [aws_internet_gateway.igw]
}

resource "aws_egress_only_internet_gateway" "ipv6_egress_igw" {
  vpc_id = aws_vpc.vpc.id

  tags = {
    Name        = "${var.operator}-${var.environment}-aws_egress_only_int_g8wy"
    operator    = var.operator
    environment = var.environment
  }
}

resource "aws_nat_gateway" "private_subnet_to_internet_gateway" {
  count             = length(aws_subnet.private_subnet)
  allocation_id     = aws_eip.nat_gateway[count.index].id
  connectivity_type = "public"

  # Provide the public (not private) subnet id. Thus the NAT Gateway will
  # have a route to the internet via the IGW.
  subnet_id = aws_subnet.public_subnet[count.index].id

  tags = {
    Name        = "${var.operator}-${var.environment}-nat-gateway-${count.index}"
    operator    = var.operator
    environment = var.environment
  }

  depends_on = [aws_internet_gateway.igw]
}

resource "aws_route" "private_subnet_to_internet_route" {
  count = length(aws_subnet.private_subnet)

  route_table_id         = aws_route_table.private_rt[count.index].id
  nat_gateway_id         = aws_nat_gateway.private_subnet_to_internet_gateway[count.index].id
  destination_cidr_block = "0.0.0.0/0"

  depends_on = [
    aws_nat_gateway.private_subnet_to_internet_gateway,
    aws_route_table.private_rt
  ]
}

resource "aws_route" "private_subnet_to_internet_ipv6_egress_only_int_g8wy_route" {
  count = length(aws_subnet.private_subnet)

  route_table_id              = aws_route_table.private_rt[count.index].id
  egress_only_gateway_id      = aws_egress_only_internet_gateway.ipv6_egress_igw.id
  destination_ipv6_cidr_block = "::/0"
}
