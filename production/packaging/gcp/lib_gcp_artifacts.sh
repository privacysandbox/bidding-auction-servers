#!/bin/bash
# Copyright 2023 Google LLC
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
function create_gcp_dist() {
  local -r server_image="$1"
  local -r dist_dir="${WORKSPACE}/dist"
  mkdir -p "${dist_dir}"/gcp
  chmod 770 "${dist_dir}" "${dist_dir}"/gcp
  cp "${WORKSPACE}/${server_image}" "${dist_dir}"/gcp
}

#######################################
# Upload image to provided repo using default gcloud config.
# If this function fails, check that your environment has permission
# to upload to the specified repo (gcloud default project, docker config, and service account).
# Arguments:
#   * the name of the service
#   * the docker image tar URI
#   * the gcp image repo
#   * the gcp image tag
#   * the build flavor
# Globals:
#   WORKSPACE
#   KOKORO_ENV_NAME (optional) name of Kokoro environment (dev or staging)
#######################################
function upload_image_to_repo() {
  local -r service="$1"
  local -r server_image="$2"
  local -r gcp_image_repo="$3"
  local -r gcp_image_tag="$4"
  local -r build_flavor="$5"

  local -r local_image_uri=bazel/production/packaging/gcp/${service}:server_docker_image
  local -r repo_image_uri="${gcp_image_repo}/${service}"
  local -r env_tag="${repo_image_uri}:${gcp_image_tag}"
  local -r git_tag="${repo_image_uri}:$(git -C "${WORKSPACE}" describe --tags --always || echo no-git-version)-${build_flavor}"

  printf "==== Uploading local image to Artifact Repository %s =====\n" "${gcp_image_repo}"
  # Note: if the following commands fail, double check that the local environment has
  # authenticated to the repo.
  docker load -i "${WORKSPACE}/${server_image}"

  local -a image_tags=("${env_tag}" "${git_tag}")
  if [[ "${KOKORO_ENV_NAME}" == "staging" ]] && [[ "${KOKORO_ENV_JOB_TYPE}" == "continuous" ]]; then
    local -r version="$(cat "${WORKSPACE}/version.txt")"
    local -r release_tag="${repo_image_uri}:staging-release-${build_flavor}-${version}"
    image_tags+=("${release_tag}")
  fi

  for tag in "${image_tags[@]}"; do
    docker tag "${local_image_uri}" "${tag}"
    docker push "${tag}"
  done

  # Get the image digest.
  # Fetched format from docker inspect is: <repo url>@sha256:<64 char hash>
  # Saved format after cut is: sha256:<64 char hash>.
  echo $(docker inspect --format='{{index .RepoDigests 0}}' "${local_image_uri}") \
   | cut -d '@' -f 2 > "${WORKSPACE}"/dist/gcp/"${service}"_"${build_flavor}".sha256
}
