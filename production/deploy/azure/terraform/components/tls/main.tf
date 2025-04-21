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

# Installs Cert-Manager to be able to issue certificates on the cluster
resource "helm_release" "cert-manager" {
  name             = "cert-manager"
  chart            = "cert-manager"
  version          = "1.17.0"
  repository       = "https://charts.jetstack.io"
  namespace        = "cert-manager"
  atomic           = true
  create_namespace = true
  cleanup_on_fail  = true

  set {
    name  = "installCRDs"
    value = "true"
  }
  set {
    name  = "podLabels.azure\\.workload\\.identity/use"
    type  = "string"
    value = "true"
  }
  set {
    name  = "serviceAccount.labels.azure\\.workload\\.identity/use"
    type  = "string"
    value = "true"
  }
}

# Installs Helm Chart with resources needed to deploy TLS on the cluster
resource "helm_release" "tls" {
  name      = "tls-chart"
  chart     = "${path.module}/tls-chart"
  namespace = var.namespace
  atomic    = true
  values = [
    templatefile("${path.module}/tls-chart/values.yaml.tftpl", {
      cert_email              = var.cert_email
      subscription_id         = var.subscription_id
      user_assigned_client_id = var.user_assigned_client_id
      agfc_resource_id        = var.agfc_resource_id
      agfc_fe_name            = var.agfc_fe_name
      namespace               = var.namespace
      fe_grpc_port            = var.fe_grpc_port
      fe_service              = var.fe_service
      dns_zone                = var.dns_zone_name
    })
  ]
}
