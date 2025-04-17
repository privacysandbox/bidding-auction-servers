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

locals {
  cloud_build_service_account = var.cloud_build_service_account_email != "" ? var.cloud_build_service_account_email : google_service_account.cloud_build_service_account[0].email
}
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

variable "cloud_build_service_account_email" {
  description = "Email address of the service account to use for cloud build triggers. If not provided, the default compute engine service account will be used."
  type = string
  default = "" # Set the default to an empty string
}
variable "cloud_build_service_account_name" {
  description = "Name of Cloud Build service account to create and assign permissions to."
  type = string
  default = ""
}

variable "cloud_build_sa_role_name" {
  description = "The custom role name for cloud build service account."
  type = string
  default = "BuildCustomRole"
}

resource "google_service_account" "cloud_build_service_account" {
  count        = var.cloud_build_service_account_email == "" && var.cloud_build_service_account_name != "" ? 1 : 0
  project      = var.project_id
  account_id   = var.cloud_build_service_account_name
  display_name = "Cloud Build Service Account"
}


resource "google_project_iam_custom_role" "cloud_build_custom_role" {
  project     = var.project_id
  role_id     = var.cloud_build_sa_role_name
  title       = "Cloud Build Custom Role"
  description = "Roles for Cloud Build Service Account"
  permissions = ["artifactregistry.repositories.downloadArtifacts", "artifactregistry.repositories.get", "artifactregistry.repositories.list","artifactregistry.repositories.listEffectiveTags","artifactregistry.repositories.listTagBindings","artifactregistry.repositories.readViaVirtualRepository", "artifactregistry.repositories.uploadArtifacts","artifactregistry.tags.create", "artifactregistry.tags.get","artifactregistry.tags.list","artifactregistry.tags.update", "artifactregistry.versions.list", "artifactregistry.versions.get", "artifactregistry.dockerimages.get", "artifactregistry.dockerimages.list", "artifactregistry.files.download","artifactregistry.files.get","artifactregistry.files.list","artifactregistry.files.update","artifactregistry.files.upload","logging.logEntries.create","logging.logEntries.route"]
}
resource "google_project_iam_member" "cloud_build_service_account_iam" {
  role    = "projects/${var.project_id}/roles/${google_project_iam_custom_role.cloud_build_custom_role.role_id}"
  member  = "serviceAccount:${local.cloud_build_service_account}"
  project = var.project_id
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
  service_account = "projects/${var.project_id}/serviceAccounts/${local.cloud_build_service_account}"
  repository_event_config {
    push {
      tag = "v.*"
    }
    repository   = var.cloud_build_linked_repository
  }
  filename = "production/packaging/gcp/cloud_build/cloudbuild.yaml"
  substitutions = {
      _GCP_IMAGE_REPO = "${var.artifact_registry_repo_location}-docker.pkg.dev/${var.project_id}/${var.artifact_registry_repo_name}/prod"
      _BUILD_FLAVOR = "prod"
  }
}


resource "google_cloudbuild_trigger" "non_prod" {
  name = "non-prod-trigger"
  project = var.project_id
  location = var.artifact_registry_repo_location
  service_account = "projects/${var.project_id}/serviceAccounts/${local.cloud_build_service_account}"
  repository_event_config {
    push {
      tag = "v.*"
    }
    repository   = var.cloud_build_linked_repository
  }
  filename = "production/packaging/gcp/cloud_build/cloudbuild.yaml"
  substitutions = {
      _GCP_IMAGE_REPO = "${var.artifact_registry_repo_location}-docker.pkg.dev/${var.project_id}/${var.artifact_registry_repo_name}/non-prod"
      _BUILD_FLAVOR = "non_prod"
  }
}

resource "google_cloudbuild_trigger" "non_prod_local_testing" {
  name = "non-prod-local-testing-trigger"
  project = var.project_id
  location = var.artifact_registry_repo_location
  service_account = "projects/${var.project_id}/serviceAccounts/${local.cloud_build_service_account}"
  repository_event_config {
    push {
      tag = "v.*"
    }
    repository   = var.cloud_build_linked_repository
  }
  filename = "production/packaging/gcp/cloud_build/cloudbuild.yaml"
  substitutions = {
      _GCP_IMAGE_REPO = "${var.artifact_registry_repo_location}-docker.pkg.dev/${var.project_id}/${var.artifact_registry_repo_name}/non-prod-local-testing"
      _BUILD_FLAVOR = "non_prod"
      _INSTANCE = "local"
  }
}
