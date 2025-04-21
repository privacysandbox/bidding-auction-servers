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

# Resource to create a OTel collector Kubernetes Service
resource "kubernetes_service" "otel-collector_service" {
  metadata {
    name      = "otel-collector-service"
    namespace = var.namespace
    labels = {
      app = "otel-collector"
    }
    annotations = {
      "service.beta.kubernetes.io/azure-load-balancer-internal" = "true"
    }
  }
  spec {
    selector = {
      app = "otel-collector"
    }
    port {
      protocol    = "TCP"
      port        = var.otel_grpc_port
      target_port = var.otel_grpc_port
      name        = "grpc"
    }
    type = "LoadBalancer"
  }
}

# Data resource to obtain IP from OTel collector IP service
data "kubernetes_service" "otel-collector_service" {
  metadata {
    name      = kubernetes_service.otel-collector_service.metadata[0].name
    namespace = kubernetes_service.otel-collector_service.metadata[0].namespace
  }
  depends_on = [kubernetes_service.otel-collector_service]
}

# Resource to create a OTel collector Kubernetes ConfigMap
resource "kubernetes_config_map" "otel-collector-config" {
  metadata {
    name      = "otel-collector-config"
    namespace = var.namespace
  }
  data = {
    "otel-collector-config.yaml" = <<YAML
receivers:
  otlp:
    protocols:
      grpc:
        endpoint: "0.0.0.0:${var.otel_grpc_port}"
processors:
  batch/traces:
    timeout: 1s
    send_batch_size: 50
  batch/metrics:
    timeout: 60s
  batch/logs:
    timeout: 60s
  filter/drop_event:
    error_mode: ignore
    logs:
      log_record:
        - 'attributes["ps_tee_log_type"] == "event_message"'
  filter/drop_non_event:
    error_mode: ignore
    logs:
      log_record:
        - 'attributes["ps_tee_log_type"] != "event_message"'
extensions:
  health_check:
exporters:
  debug/detailed:
    verbosity: detailed
  azuremonitor/otel:
    connection_string: "${var.application_insights_otel_connection_string}"
    instrumentation_key: "${var.application_insights_otel_instrumentation_key}"
    spaneventsenabled: true
service:
  pipelines:
    traces:
      receivers: [otlp]
      processors: [batch/traces]
      exporters: [azuremonitor/otel,debug/detailed]
    metrics:
      receivers: [otlp]
      processors: [batch/metrics]
      exporters: [azuremonitor/otel,debug/detailed]
    logs:
      receivers: [otlp]
      processors: [batch/logs,filter/drop_event,filter/drop_non_event]
      exporters: [azuremonitor/otel,debug/detailed]
  telemetry:
    logs:
YAML
  }
}

# Resource to create a OTel collector Kubernetes Deployment
resource "kubernetes_deployment" "otel-collector-deployment" {
  metadata {
    name      = "otel-collector"
    namespace = var.namespace
    labels = {
      app = "otel-collector"
    }
  }

  spec {
    replicas = var.otel_replicas

    selector {
      match_labels = {
        app = "otel-collector"
      }
    }

    template {
      metadata {
        labels = {
          app = "otel-collector"
        }
      }

      spec {
        container {
          name  = "otel-collector"
          image = "otel/opentelemetry-collector-contrib:latest"
          env {
            name  = "INSTRUMENTATION_KEY"
            value = var.application_insights_otel_instrumentation_key
          }
          args = ["--config=/etc/otel/otel-config.yaml"]
          volume_mount {
            name       = "otel-config-volume"
            mount_path = "/etc/otel/otel-config.yaml"
            sub_path   = "otel-collector-config.yaml"
          }
          port {
            container_port = var.otel_grpc_port
          }
        }

        volume {
          name = "otel-config-volume"
          config_map {
            name = kubernetes_config_map.otel-collector-config.metadata[0].name
          }
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
