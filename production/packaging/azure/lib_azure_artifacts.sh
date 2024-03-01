#!/bin/bash
# Copyright (C) Microsoft Corporation. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#######################################
# Copy build artifacts to the workspace's dist/gcp.
# Arguments:
#   * the docker image tar URI
# Globals:
#   WORKSPACE
#######################################
function create_azure_dist() {
  local -r server_image="$1"
  local -r dist_dir="${WORKSPACE}/dist"
  mkdir -p "${dist_dir}"/azure
  chmod 770 "${dist_dir}" "${dist_dir}"/azure
  cp "${WORKSPACE}/${server_image}" "${dist_dir}"/azure
}

#######################################
# Upload image to provided repo using default gcloud config.
# If this function fails, check that your environment has permission
# to upload to the specified repo (gcloud default project, docker config, and service account).
# Arguments:
#   * the name of the service
#   * the docker image tar URI
#   * the azure image repo
#   * the azure image tag
#   * the build flavor
# Globals:
#   WORKSPACE
#######################################
function upload_image_to_repo() {
  local -r service="$1"
  local -r server_image="$2"
  local -r azure_image_repo="$3"
  local -r azure_image_tag="$4"
  local -r build_flavor="$5"

  local -r local_image_uri=bazel/production/packaging/azure/${service}:server_docker_image
  local -r repo_image_uri="${azure_image_repo}/${service}"
  local -r env_tag="${repo_image_uri}:${azure_image_tag}"
  local -r git_tag="${repo_image_uri}:$(git -C ${WORKSPACE} describe --tags --always || echo no-git-version)-${build_flavor}"

  printf "==== Uploading local image to Artifact Repository ${azure_image_repo} =====\n"
  # Note: if the following commands fail, double check that the local environment has
  # authenticated to the repo.
  docker load -i "${WORKSPACE}/${server_image}"

  # Push with env tag.
  docker tag "${local_image_uri}" "${env_tag}"
  docker push "${env_tag}"

  # Push with git tag.
  docker tag "${local_image_uri}" "${git_tag}"
  docker push "${git_tag}"

  # Get the image digest.
  # Fetched format from docker inspect is: <repo url>@sha256:<64 char hash>
  # Saved format after cut is: sha256:<64 char hash>.
  echo $(docker inspect --format='{{index .RepoDigests 0}}' "${local_image_uri}") \
   | cut -d '@' -f 2 > "${WORKSPACE}"/dist/azure/"${service}"_"${build_flavor}".sha256
}
