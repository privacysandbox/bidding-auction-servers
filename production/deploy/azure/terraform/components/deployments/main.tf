/**
 * Copyright 2025 Google LLC
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

resource "kubernetes_deployment" "frontend_service_deployment" {
  metadata {
    name      = "${var.fe_service}-deployment"
    namespace = var.namespace
  }
  spec {
    replicas = var.fe_replicas
    selector {
      match_labels = {
        app = var.fe_service
      }
    }
    template {
      metadata {
        annotations = {
          configmap_hash                                            = var.parc_config_map_hash
          "microsoft.containerinstance.virtualnode.injectdns"       = "false"
          "microsoft.containerinstance.virtualnode.injectkubeproxy" = "false"
          "microsoft.containerinstance.virtualnode.ccepolicy"       = var.fe_ccepolicy
        }
        labels = {
          app = var.fe_service
        }
      }
      spec {
        host_network = false
        security_context {
          seccomp_profile {
            type = "Unconfined"
          }
        }
        container {
          name    = "${var.fe_service}-container"
          image   = var.fe_image
          command = ["/server/bin/init_server_basic"]
          env {
            name = "POD_LABELS_JSON"
            value = jsonencode({
              environment = var.environment
              operator    = var.operator
              service     = var.fe_service
              region      = var.region
              zone        = var.zone
            })
          }
          env {
            name = "POD_RESOURCE_ID"
            value_from {
              field_ref {
                field_path = "metadata.name"
              }
            }
          }

          port {
            container_port = var.fe_grpc_port
          }
          resources {
            limits = {
              cpu    = var.cpu_limit
              memory = var.memory_limit
            }
          }
          readiness_probe {
            grpc {
              port = var.fe_grpc_port
            }
            initial_delay_seconds = 45
            period_seconds        = 10
            timeout_seconds       = 5
            success_threshold     = 1
            failure_threshold     = 3
          }
          liveness_probe {
            grpc {
              port = var.fe_grpc_port
            }
            initial_delay_seconds = 45
            period_seconds        = 10
            timeout_seconds       = 5
            success_threshold     = 1
            failure_threshold     = 3
          }
          startup_probe {
            grpc {
              port = var.fe_grpc_port
            }
            initial_delay_seconds = 45
            period_seconds        = 10
            timeout_seconds       = 5
            success_threshold     = 1
            failure_threshold     = 3
          }
        }
        node_selector = {
          virtualization = "virtualnode2"
        }
        toleration {
          effect   = "NoSchedule"
          key      = "virtual-kubelet.io/provider"
          operator = "Exists"
        }
      }
    }
  }
  timeouts {
    create = "5m"
    update = "5m"
    delete = "5m"
  }
}

resource "kubernetes_deployment" "service_deployment" {
  metadata {
    name      = "${var.service}-deployment"
    namespace = var.namespace
  }
  spec {
    replicas = var.replicas
    selector {
      match_labels = {
        app = var.service
      }
    }
    template {
      metadata {
        annotations = {
          configmap_hash                                            = var.parc_config_map_hash
          "microsoft.containerinstance.virtualnode.injectdns"       = "false"
          "microsoft.containerinstance.virtualnode.injectkubeproxy" = "false"
          "microsoft.containerinstance.virtualnode.ccepolicy"       = var.ccepolicy
        }
        labels = {
          app = var.service
        }
      }
      spec {
        host_network = false
        security_context {
          seccomp_profile {
            type = "Unconfined"
          }
        }
        container {
          name              = "${var.service}-container"
          image             = var.image
          image_pull_policy = "IfNotPresent"
          command           = ["/server/bin/init_server_basic"]
          env {
            name = "POD_LABELS_JSON"
            value = jsonencode({
              environment = var.environment
              operator    = var.operator
              service     = var.service
              region      = var.region
              zone        = var.zone
            })
          }
          env {
            name = "POD_RESOURCE_ID"
            value_from {
              field_ref {
                field_path = "metadata.name"
              }
            }
          }
          port {
            container_port = var.grpc_port
          }
          resources {
            limits = {
              cpu    = var.cpu_limit
              memory = var.memory_limit
            }
          }
          readiness_probe {
            grpc {
              port = var.grpc_port
            }
            initial_delay_seconds = 45
            period_seconds        = 10
            timeout_seconds       = 5
            success_threshold     = 1
            failure_threshold     = 3
          }
          liveness_probe {
            grpc {
              port = var.grpc_port
            }
            initial_delay_seconds = 45
            period_seconds        = 10
            timeout_seconds       = 5
            success_threshold     = 1
            failure_threshold     = 3
          }
          startup_probe {
            grpc {
              port = var.grpc_port
            }
            initial_delay_seconds = 45
            period_seconds        = 10
            timeout_seconds       = 5
            success_threshold     = 1
            failure_threshold     = 3
          }
        }
        node_selector = {
          virtualization = "virtualnode2"
        }
        toleration {
          effect   = "NoSchedule"
          key      = "virtual-kubelet.io/provider"
          operator = "Exists"
        }
      }
    }
  }
  timeouts {
    create = "5m"
    update = "5m"
    delete = "5m"
  }
}
