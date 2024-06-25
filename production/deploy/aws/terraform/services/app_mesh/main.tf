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

resource "aws_appmesh_mesh" "app_mesh" {
  name = "${var.operator}-${var.environment}-app-mesh"
}

resource "aws_service_discovery_private_dns_namespace" "cloud_map_private_dns_namespace" {
  name = "${var.operator}-${var.environment}-cloud-map-private-dns-namespace"
  vpc  = var.vpc_id
}
