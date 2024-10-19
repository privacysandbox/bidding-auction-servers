#!/bin/bash
# Copyright 2022 Google LLC
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

set -o errexit
set -o pipefail

declare -r arch=`arch`
if [[ ${arch} != "x86_64" && ${arch} != "aarch64" ]]; then
  printf "Unrecognized or unsupported architecture for nitro: %s\n" "${arch}" &>/dev/stderr
  return 1
fi

# Install necessary dependencies
sudo dnf update -y
sudo dnf install -y \
  "amazon-cloudwatch-agent-1.300044.0-1.amzn2023.${arch}" \
  "docker-25.0.6-1.amzn2023.0.2.${arch}"

wget -O /tmp/otelcol-contrib_0.105.0_linux_amd64.rpm https://github.com/open-telemetry/opentelemetry-collector-releases/releases/download/v0.105.0/otelcol-contrib_0.105.0_linux_amd64.rpm
sudo yum localinstall -y /tmp/otelcol-contrib_0.105.0_linux_amd64.rpm
ENV_FILE="/etc/otelcol-contrib/otelcol-contrib.conf"
NEW_OTELCOL_OPTIONS="OTELCOL_OPTIONS=\"--config=/opt/privacysandbox/otel_collector_config.yaml\""
sudo bash -c "sudo sed -i 's|^OTELCOL_OPTIONS=.*|${NEW_OTELCOL_OPTIONS}|' $ENV_FILE"

sudo dnf install -y "aws-nitro-enclaves-cli-1.3.1-0.amzn2023.${arch}"

sudo usermod -a -G docker ec2-user
sudo usermod -a -G ne ec2-user

sudo systemctl start docker
sudo systemctl enable docker
sudo docker pull envoyproxy/envoy-distroless-dev:e0cc4306253f0318f9dcbc77cf32feb6245c378f
