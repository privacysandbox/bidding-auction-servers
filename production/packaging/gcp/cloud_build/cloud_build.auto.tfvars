# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http: //www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

project_id = "gtech-privacy-baservices-dev"
artifact_registry_repo_name = "bidding-auction-servers-image-repo"
artifact_registry_repo_location = "us-central1"
cloud_build_linked_repository = "projects/gtech-privacy-baservices-dev/locations/us-central1/connections/github-seburan/repositories/Seburan-bidding-auction-servers"

# [1] Uncomment below lines if you like Terraform grant needed permissions to
# pre-existing service accounts
# cloud_build_service_account_email = "build-sa@<project>.iam.gserviceaccount.com"

# [2] Uncomment below lines if you like Terraform to create service accounts
# and needed permissions granted e.g "build-sa" or "worker-sa"
# cloud_build_service_account_name = "build-sa"
