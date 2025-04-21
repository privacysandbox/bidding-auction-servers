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

# Creates the Kubernetes service for frontend servers. Ex: SFE and BFE
# Uses the cluster context defined by the parent module's kubernetes provider settings to create the service within the correct AKS cluster.
resource "kubernetes_service" "fe_service" {
  metadata {
    name      = var.fe_service
    namespace = var.namespace
    annotations = {
      # External traffic is expected to be routed via traffic manager global load balancer to AGfC regional load balancer to the frontend service.
      # The service IP is not expected to be accessible outside of the vnet.
      "service.beta.kubernetes.io/azure-load-balancer-internal" : "true"
    }
  }
  spec {
    selector = {
      app = var.fe_service
    }
    port {
      name        = "grpc"
      protocol    = "TCP"
      port        = var.fe_grpc_port
      target_port = var.fe_grpc_port
    }
    dynamic "port" {
      for_each = var.fe_http_port != null ? [var.fe_http_port] : []
      content {
        name        = "http"
        protocol    = "TCP"
        port        = var.fe_http_port
        target_port = var.fe_http_port
      }
    }
    type = "LoadBalancer"
  }
}

# Data resource provides the IP address of the Frontend Service
data "kubernetes_service" "fe_service" {
  metadata {
    name      = kubernetes_service.fe_service.metadata[0].name
    namespace = kubernetes_service.fe_service.metadata[0].namespace
  }
  depends_on = [kubernetes_service.fe_service]
}

# Create Kubernetes service for the backend servers. Ex: Auction and Bidding
resource "kubernetes_service" "service" {
  metadata {
    name      = var.service
    namespace = var.namespace
    annotations = {
      "service.beta.kubernetes.io/azure-load-balancer-internal" : "true"
    }
  }
  spec {
    selector = {
      app = var.service
    }
    port {
      name        = "grpc"
      protocol    = "TCP"
      port        = var.grpc_port
      target_port = var.grpc_port
    }
    type = "LoadBalancer"
  }
}

# Data resource provides the IP address of the Kubernetes Service
data "kubernetes_service" "service" {
  metadata {
    name      = kubernetes_service.service.metadata[0].name
    namespace = kubernetes_service.service.metadata[0].namespace
  }
  depends_on = [kubernetes_service.service]
}
