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

# Helm Release to install virtual nodes inside the cluster
resource "helm_release" "virtual_node" {
  name             = "${var.operator}-${var.environment}-${var.region}-vn2"
  repository       = "https://microsoft.github.io/virtualnodesOnAzureContainerInstances"
  chart            = "virtualnode"
  create_namespace = true
  timeout          = 600
  atomic           = true

  values = [
    "${file("${path.module}/values.yaml")}"
  ]

  set {
    name  = "namespace"
    value = "vn2"
  }

  set {
    name  = "replicaCount"
    value = var.vn_replica_count
  }

  set {
    name  = "admissionControllerReplicaCount"
    value = var.vn_admission_controller_replica_count
  }

  set {
    name  = "nodeLabels"
    value = "container-image=vn2"
  }


}
