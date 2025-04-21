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

# Resource to create Parc Kubernetes Service
resource "kubernetes_service" "parc_service" {
  metadata {
    name      = "${var.service}-service"
    namespace = var.namespace
    labels = {
      app = var.service
    }
    annotations = {
      "service.beta.kubernetes.io/azure-load-balancer-internal" = "true"
    }
  }
  spec {
    selector = {
      app = var.service
    }
    port {
      protocol    = "TCP"
      port        = var.parc_port
      target_port = var.parc_port
      name        = "grpc"
    }
    type = "LoadBalancer"
  }
}

# Data resource to obtain Parc server Service IP Address
data "kubernetes_service" "parc_service" {
  metadata {
    name      = kubernetes_service.parc_service.metadata[0].name
    namespace = kubernetes_service.parc_service.metadata[0].namespace
  }
}

# Resource to create Parc server Kubernetes Deployment
resource "kubernetes_deployment" "parc-deployment" {
  metadata {
    name      = "${var.service}-deployment"
    namespace = var.namespace
    labels = {
      app = var.service
    }
  }

  spec {
    replicas = 1

    selector {
      match_labels = {
        app = var.service
      }
    }

    template {
      metadata {
        labels = {
          app = var.service
        }
        annotations = {
          configmap_hash = sha256(jsonencode(kubernetes_config_map.parc-parameters.data))
        }
      }

      spec {
        container {
          name              = "${var.service}-container"
          image             = var.parc_image
          image_pull_policy = "IfNotPresent"
          args = [
            "--address=0.0.0.0",
            "--port=${var.parc_port}",
            "--verbose",
            "--parameters_file_path=/parc/data/parameters.jsonl",
            "--otel_collector=${var.otel_collector_endpoint}:${var.otel_grpc_port}"
          ]
          volume_mount {
            name       = "${var.service}-parameters"
            mount_path = "/parc/data"
            read_only  = true
          }
          port {
            container_port = var.parc_port
          }
        }

        volume {
          name = "${var.service}-parameters"
          config_map {
            name = "${var.service}-parameters"
          }
        }
      }
    }
  }
  timeouts {
    create = "2m"
    update = "2m"
    delete = "2m"
  }
}

# Resource to create Parc server Kubernetes ConfigMap
resource "kubernetes_config_map" "parc-parameters" {
  metadata {
    name      = "${var.service}-parameters"
    namespace = var.namespace
  }

  data = {
    "parameters.jsonl" = resource.local_file.updated_app_parameters_jsonl.content
  }
}

# Variables and Resource to create a JSONL file with operator and environment tagged parameters from a JSON file.
locals {
  original_json = jsondecode(file("${path.root}/${var.operator}_app_parameters.json"))

  # Corrected syntax for the 'for' expression inside the list constructor
  updated_jsonl = join("\n", [
    for key, value in local.original_json : jsonencode({
      key   = "${var.operator}-${var.environment}-${key}"
      value = value
    })
  ])
}

resource "local_file" "updated_app_parameters_jsonl" {
  filename = "${path.module}/${var.operator}_${var.environment}_updated_app_parameters.jsonl"
  content  = local.updated_jsonl
}
