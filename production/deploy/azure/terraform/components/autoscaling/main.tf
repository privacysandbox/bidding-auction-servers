/**
 * Copyright 2023 Google LLC
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

resource "kubernetes_horizontal_pod_autoscaler_v2" "fe_service_hpa" {
  metadata {
    name      = "${var.fe_service}-hpa"
    namespace = var.namespace
  }
  spec {
    scale_target_ref {
      api_version = "apps/v1"
      kind        = "Deployment"
      name        = var.frontend_deployment_name
    }
    min_replicas = var.hpa_fe_min_replicas
    max_replicas = var.hpa_fe_max_replicas
    metric {
      type = "Resource"
      resource {
        name = "cpu"
        target {
          type                = "Utilization"
          average_utilization = var.hpa_fe_avg_cpu_utilization
        }
      }
    }
  }
}

resource "kubernetes_horizontal_pod_autoscaler_v2" "service_hpa" {
  metadata {
    name      = "${var.service}-hpa"
    namespace = var.namespace
  }
  spec {
    scale_target_ref {
      api_version = "apps/v1"
      kind        = "Deployment"
      name        = var.backend_deployment_name
    }
    min_replicas = var.hpa_min_replicas
    max_replicas = var.hpa_max_replicas
    metric {
      type = "Resource"
      resource {
        name = "cpu"
        target {
          type                = "Utilization"
          average_utilization = var.hpa_avg_cpu_utilization
        }
      }
    }
  }
}
