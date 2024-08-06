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

# Install necessary dependencies
sudo yum update -y
sudo yum install -y \
  amazon-cloudwatch-agent \
  docker
wget -O /tmp/aws-otel-collector.rpm https://aws-otel-collector.s3.amazonaws.com/amazon_linux/amd64/latest/aws-otel-collector.rpm
sudo yum localinstall -y /tmp/aws-otel-collector.rpm
sudo amazon-linux-extras install -y aws-nitro-enclaves-cli

sudo usermod -a -G docker ec2-user
sudo usermod -a -G ne ec2-user

sudo systemctl start docker
sudo systemctl enable docker
sudo docker pull envoyproxy/envoy-distroless-dev:e0cc4306253f0318f9dcbc77cf32feb6245c378f
