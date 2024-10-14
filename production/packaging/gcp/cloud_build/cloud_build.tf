# Copyright 2024 Google LLC
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

variable "artifact_registry_repo_name" {
  description = "The artifact registry name. Will be located at us-docker.pkg.dev/var.project_id/var.artifact_registry_name."
}

variable "artifact_registry_repo_location" {
  description = "The artifact registry name. Will be located at us-docker.pkg.dev/var.project_id/var.artifact_registry_name."
  default = "us-central1"
}

variable "cloud_build_linked_repository" {
    description = "Name of manually added cloud build 2nd gen repository. Learn more here: https://cloud.google.com/build/docs/repositories"
}

variable "project_id" {
  description = "The Google Cloud project ID"
}

resource "google_artifact_registry_repository" "cloudbuild_repo" {
  project = var.project_id
  location    = var.artifact_registry_repo_location
  format      = "docker"
  repository_id = var.artifact_registry_repo_name
}

resource "google_cloudbuild_trigger" "prod" {
  name = "prod-trigger"
  project = var.project_id
  location = var.artifact_registry_repo_location
  repository_event_config {
    push {
      tag = "v.*"
    }
    repository   = var.cloud_build_linked_repository
  }
  filename = "production/packaging/gcp/cloud_build/cloudbuild.yaml"
  substitutions = {
      _GCP_IMAGE_REPO = "us-docker.pkg.dev/${var.project_id}/${var.artifact_registry_repo_name}/prod"
      _BUILD_FLAVOR = "prod"
  }
}


resource "google_cloudbuild_trigger" "non_prod" {
  name = "non-prod-trigger"
  project = var.project_id
  location = var.artifact_registry_repo_location
  repository_event_config {
    push {
      tag = "v.*"
    }
    repository   = var.cloud_build_linked_repository
  }
  filename = "production/packaging/gcp/cloud_build/cloudbuild.yaml"
  substitutions = {
      _GCP_IMAGE_REPO = "us-docker.pkg.dev/${var.project_id}/${var.artifact_registry_repo_name}/non-prod"
      _BUILD_FLAVOR = "non_prod"
  }
}
