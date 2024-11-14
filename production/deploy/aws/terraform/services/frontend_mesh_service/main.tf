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

resource "aws_service_discovery_service" "cloud_map_service" {
  name = "${var.service}-${var.operator}-${var.environment}-cloud-map-service.${var.root_domain}"

  dns_config {
    namespace_id = var.cloud_map_private_dns_namespace_id

    dns_records {
      ttl  = 10
      type = "A"
    }
  }

  health_check_custom_config {
    failure_threshold = 1
  }

  # Ensure all cloud map entries are deleted.
  force_destroy = true
}

resource "aws_appmesh_virtual_node" "appmesh_virtual_node_with_tls" {
  # Only create this version of the node if using TLS.
  count = (var.use_tls_with_mesh) ? 1 : 0

  name      = "${var.service}-${var.operator}-${var.environment}-appmesh-virtual-node"
  mesh_name = var.app_mesh_id
  spec {
    backend {
      virtual_service {
        virtual_service_name = var.backend_virtual_service_name
      }
    }

    backend_defaults {
      client_policy {
        tls {
          enforce = true
          validation {
            trust {
              acm {
                certificate_authority_arns = [var.backend_virtual_service_private_certificate_arn]
              }
            }
            subject_alternative_names {
              match {
                exact = [
                  "${var.service}-${var.operator}-${var.environment}-appmesh-virtual-service.${var.root_domain}.${var.cloud_map_private_dns_namespace_name}",
                  "${var.backend_service}-${var.operator}-${var.environment}-appmesh-virtual-service.${var.root_domain}.${var.cloud_map_private_dns_namespace_name}"
                ]
              }
            }
          }
        }
      }
    }

    listener {
      port_mapping {
        port     = 443
        protocol = "grpc"
      }

      health_check {
        protocol            = "grpc"
        healthy_threshold   = var.healthcheck_healthy_threshold
        unhealthy_threshold = var.healthcheck_unhealthy_threshold
        timeout_millis      = var.healthcheck_timeout_sec * 1000
        interval_millis     = var.healthcheck_interval_sec * 1000
      }
    }

    service_discovery {
      aws_cloud_map {
        service_name   = aws_service_discovery_service.cloud_map_service.name
        namespace_name = var.cloud_map_private_dns_namespace_name
      }
    }
  }
}

resource "aws_appmesh_virtual_node" "appmesh_virtual_node_sans_tls" {
  # Only create this version of the node if NOT using TLS.
  count = (var.use_tls_with_mesh) ? 0 : 1

  name      = "${var.service}-${var.operator}-${var.environment}-appmesh-virtual-node"
  mesh_name = var.app_mesh_id
  spec {
    backend {
      virtual_service {
        virtual_service_name = var.backend_virtual_service_name
      }
    }

    listener {
      port_mapping {
        port     = 443
        protocol = "grpc"
      }

      health_check {
        protocol            = "grpc"
        healthy_threshold   = var.healthcheck_healthy_threshold
        unhealthy_threshold = var.healthcheck_unhealthy_threshold
        timeout_millis      = var.healthcheck_timeout_sec * 1000
        interval_millis     = var.healthcheck_interval_sec * 1000
      }
    }

    service_discovery {
      aws_cloud_map {
        service_name   = aws_service_discovery_service.cloud_map_service.name
        namespace_name = var.cloud_map_private_dns_namespace_name
      }
    }
  }
}

resource "aws_appmesh_virtual_service" "appmesh_virtual_service" {
  name      = "${var.service}-${var.operator}-${var.environment}-appmesh-virtual-service.${var.root_domain}"
  mesh_name = var.app_mesh_name
  spec {
    provider {
      virtual_node {
        virtual_node_name = (var.use_tls_with_mesh) ? aws_appmesh_virtual_node.appmesh_virtual_node_with_tls[0].name : aws_appmesh_virtual_node.appmesh_virtual_node_sans_tls[0].name
      }
    }
  }
}

resource "aws_route53_record" "mesh_node_record" {
  name = aws_appmesh_virtual_service.appmesh_virtual_service.name
  type = "A"
  // In seconds
  ttl     = 300
  zone_id = var.root_domain_zone_id

  # Set-identifier is used to ensure that our services can reference the same
  # DNS record name across different regions, while still allowing the name to
  # point to a different record in each region. This is necessary because
  # without set-identifier, the 'name' attriubute is used as the identifier, and
  # causes an error when trying to create the same Route53 record in
  # multiple regions. To temporarily address this issue, we used the
  # latency_routing_policy with the set identifier tag that has no direct use,
  # but will be resolved at a later date with b/376858399.

  # See the Route53 Record documentation for more details:
  # https://registry.terraform.io/providers/hashicorp/aws/latest/docs/resources/route53_record

  # One quirk with set_identifier is that it doesn't work with simple routing
  # policies.
  set_identifier = "${aws_appmesh_virtual_service.appmesh_virtual_service.name}-${var.region}-mesh-node-record"

  latency_routing_policy {
    region = var.region
  }

  // Any non-loopback IP will do, this record just needs to exist, not go anywhere (should be overrifed by appmesh).
  records = ["10.10.10.10"]
}

data "aws_iam_policy_document" "virtual_node_policy_document" {
  statement {
    actions = [
      "appmesh:StreamAggregatedResources"
    ]
    resources = [
      (var.use_tls_with_mesh) ? aws_appmesh_virtual_node.appmesh_virtual_node_with_tls[0].arn : aws_appmesh_virtual_node.appmesh_virtual_node_sans_tls[0].arn
    ]
  }
}

resource "aws_iam_policy" "app_mesh_node_policy" {
  name   = format("%s-%s-%s-%s-virtualNodePolicy", var.service, var.operator, var.environment, var.region)
  policy = data.aws_iam_policy_document.virtual_node_policy_document.json
}

resource "aws_iam_role_policy_attachment" "app_mesh_node_policy_to_ec2_attachment" {
  role       = var.server_instance_role_name
  policy_arn = aws_iam_policy.app_mesh_node_policy.arn
}

resource "aws_iam_role_policy_attachment" "amazon_ec2_container_registry_read_only_to_ec2_attachment" {
  role       = var.server_instance_role_name
  policy_arn = "arn:aws:iam::aws:policy/AmazonEC2ContainerRegistryReadOnly"
}
