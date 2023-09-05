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

###############################################################
#
#              SERVICE MESH (INTERNAL LOAD BALANCER)
#
# The service mesh uses HTTP/2 (gRPC) with no TLS.
###############################################################

resource "google_network_services_grpc_route" "default" {
  provider  = google-beta
  name      = "${var.operator}-${var.environment}-frontend-to-backend"
  hostnames = [split("///", var.backend_address)[1]]
  meshes    = [var.mesh.id]
  rules {
    action {
      destinations {
        service_name = "projects/${var.gcp_project_id}/locations/global/backendServices/${google_compute_backend_service.mesh_backend.name}"
      }
    }
  }
}

resource "google_compute_backend_service" "mesh_backend" {
  name                  = "${var.operator}-${var.environment}-mesh-backend-service"
  provider              = google-beta
  port_name             = "grpc"
  protocol              = "GRPC"
  load_balancing_scheme = "INTERNAL_SELF_MANAGED"
  locality_lb_policy    = "ROUND_ROBIN"
  timeout_sec           = 10
  health_checks         = [google_compute_health_check.backend.id]

  dynamic "backend" {
    for_each = var.backend_instance_groups
    content {
      group           = backend.value
      balancing_mode  = "UTILIZATION"
      max_utilization = 0.80
      capacity_scaler = 1.0
    }
  }
}


resource "google_compute_health_check" "backend" {
  name = "${var.operator}-${var.environment}-${var.backend_service_name}-lb-hc"
  grpc_health_check {
    port_name = "grpc"
    port      = var.backend_service_port
  }

  timeout_sec         = 3
  check_interval_sec  = 3
  healthy_threshold   = 2
  unhealthy_threshold = 4

  log_config {
    enable = true
  }
}


###############################################################
#
#                         Collector LB
#
# The external lb uses HTTP/2 (gRPC) with no TLS.
###############################################################

resource "google_compute_backend_service" "mesh_collector" {
  name     = "${var.operator}-${var.environment}-mesh-collector-service"
  provider = google-beta

  port_name             = "otlp"
  protocol              = "TCP"
  load_balancing_scheme = "EXTERNAL"
  timeout_sec           = 10
  health_checks         = [google_compute_health_check.collector.id]

  dynamic "backend" {
    for_each = var.collector_instance_groups
    content {
      group           = backend.value
      balancing_mode  = "UTILIZATION"
      capacity_scaler = 1.0
    }
  }
  depends_on = [var.mesh, google_network_services_grpc_route.default]
}

resource "google_compute_target_tcp_proxy" "collector" {
  name            = "${var.operator}-${var.environment}-${var.collector_service_name}-lb-proxy"
  backend_service = google_compute_backend_service.mesh_collector.id
}

resource "google_compute_global_forwarding_rule" "collector" {
  name     = "${var.operator}-${var.environment}-${var.collector_service_name}-forwarding-rule"
  provider = google-beta

  ip_protocol           = "TCP"
  port_range            = var.collector_service_port
  load_balancing_scheme = "EXTERNAL"
  target                = google_compute_target_tcp_proxy.collector.id
  ip_address            = var.collector_ip_address

  labels = {
    environment = var.environment
    operator    = var.operator
    service     = var.collector_service_name
  }
}

resource "google_dns_record_set" "collector" {
  name         = "${var.collector_service_name}-${var.operator}-${var.environment}.${var.frontend_domain_name}."
  managed_zone = var.frontend_dns_zone
  type         = "A"
  ttl          = 10
  rrdatas = [
    var.collector_ip_address
  ]
}


resource "google_compute_health_check" "collector" {
  name = "${var.operator}-${var.environment}-${var.collector_service_name}-lb-hc"

  tcp_health_check {
    port_name = "otlp"
    port      = var.collector_service_port
  }

  timeout_sec         = 3
  check_interval_sec  = 3
  healthy_threshold   = 2
  unhealthy_threshold = 4

  log_config {
    enable = true
  }
}


###############################################################
#
#                         EXTERNAL LB
#
# The external lb uses HTTP/2 (gRPC) with TLS.
###############################################################

resource "google_compute_backend_service" "default" {
  name                  = "${var.operator}-${var.environment}-xlb-backend-service"
  provider              = google-beta
  port_name             = "grpc"
  protocol              = "HTTP2"
  load_balancing_scheme = "EXTERNAL_MANAGED"
  locality_lb_policy    = "ROUND_ROBIN"
  timeout_sec           = 10
  health_checks         = [google_compute_health_check.frontend.id]
  dynamic "backend" {
    for_each = var.frontend_instance_groups
    content {
      group           = backend.value
      balancing_mode  = "UTILIZATION"
      max_utilization = 0.80
      capacity_scaler = 1.0
    }
  }

  depends_on = [var.mesh, google_network_services_grpc_route.default]
}

resource "google_compute_url_map" "default" {
  name            = "${var.operator}-${var.environment}-xlb-grpc-map"
  default_service = google_compute_backend_service.default.id
}


resource "google_compute_target_https_proxy" "default" {
  name    = "${var.operator}-${var.environment}-https-lb-proxy"
  url_map = google_compute_url_map.default.id
  ssl_certificates = [
    var.frontend_domain_ssl_certificate_id
  ]
}

resource "google_compute_global_forwarding_rule" "xlb_https" {
  name     = "${var.operator}-${var.environment}-xlb-https-forwarding-rule"
  provider = google-beta

  ip_protocol           = "TCP"
  port_range            = "443"
  load_balancing_scheme = "EXTERNAL_MANAGED"
  target                = google_compute_target_https_proxy.default.id
  ip_address            = var.frontend_ip_address

  labels = {
    environment = var.environment
    operator    = var.operator
    service     = var.frontend_service_name
  }
}

resource "google_dns_record_set" "default" {
  name         = "${var.operator}-${var.environment}.${var.frontend_domain_name}."
  managed_zone = var.frontend_dns_zone
  type         = "A"
  ttl          = 10
  rrdatas = [
    var.frontend_ip_address
  ]
}

resource "google_compute_health_check" "frontend" {
  name = "${var.operator}-${var.environment}-${var.frontend_service_name}-lb-hc"
  # gpc_health_check does not support TLS
  # Workaround: use tcp
  # Details: https://cloud.google.com/load-balancing/docs/health-checks#optional-flags-hc-protocol-grpc
  tcp_health_check {
    port_name = "grpc"
    port      = var.frontend_service_port
  }

  timeout_sec         = 3
  check_interval_sec  = 3
  healthy_threshold   = 2
  unhealthy_threshold = 4

  log_config {
    enable = true
  }
}
