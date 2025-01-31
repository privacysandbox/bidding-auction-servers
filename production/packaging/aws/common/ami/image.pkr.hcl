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

variable "regions" {
  type    = list(string)
  validation {
    condition     = length(var.regions) > 0
    error_message = <<EOF
The regions var is not set. Must specify at least one region.
EOF
  }
}

variable "git_commit" {
  type    = string
}

variable "build_version" {
  type    = string
  description    = "git tag if available"
}

variable "build_flavor" {
  type    = string
  description = "prod or non_prod"
}

variable "build_env" {
  type    = string
}

variable "service" {
  type    = string
  validation {
  condition     = length(var.service) > 0
  error_message = <<EOF
The service var is not set.
EOF
  }
}

# Directory path where the built artifacts appear
variable "distribution_dir" {
  type = string
  default = env("DIST")
  validation {
    condition     = length(var.distribution_dir) > 0
    error_message = <<EOF
The distribution_dir var is not set: make sure to at least set the DIST env var.
To fix this you could also set the distribution_dir variable from the arguments, for example:
$ packer build -var=distribution_dir=/src/workspace/dist/aws ...
EOF
  }
}

# Directory path of the project repository
variable "workspace" {
  type = string
  default = env("WORKSPACE")
  validation {
    condition     = length(var.workspace) > 0
    error_message = <<EOF
The workspace var is not set: make sure to at least set the WORKSPACE env var.
To fix this you could also set the workspace variable from the arguments, for example:
$ packer build -var=workspace=/src/workspace ...
EOF
  }
}

locals { timestamp = regex_replace(timestamp(), "[- TZ:]", "") }

# source blocks are generated from your builders; a source can be referenced in
# build blocks. A build block runs provisioners and post-processors on a
# source.
source "amazon-ebs" "dataserver" {
  ami_name      = "${var.service}-${var.build_version}-${var.build_flavor}-${local.timestamp}"
  instance_type = "m5.xlarge"
  region        = var.regions[0]
  ami_regions   = var.regions
  source_ami_filter {
    filters = {
      name                = "al2023-ami-20*-kernel-*-x86_64"
      root-device-type    = "ebs"
      virtualization-type = "hvm"
    }
    most_recent = true
    owners      = ["137112412989"]
  }
  tags = {
    git_commit = var.git_commit
    build_version = var.build_version
    build_flavor = var.build_flavor
    build_env = var.build_env
  }
  ssh_username = "ec2-user"
}

# a build block invokes sources and runs provisioning steps on them.
build {
  sources = ["source.amazon-ebs.dataserver"]

  provisioner "file" {
    source      = join("/", [var.distribution_dir, "/proxy"])
    destination = "/tmp/proxy"
  }
  provisioner "file" {
    source      = join("/", [var.distribution_dir, "/bidding_auction_servers_descriptor_set.pb"])
    destination = "/tmp/bidding_auction_servers_descriptor_set.pb"
  }
  provisioner "file" {
    source      = join("/", [var.distribution_dir, "/envoy.yaml"])
    destination = "/tmp/envoy.yaml"
  }
  provisioner "file" {
    source      = join("/", [var.distribution_dir, "otel_collector_config.yaml"])
    destination = "/tmp/otel_collector_config.yaml"
  }
  provisioner "file" {
    source      = join("/", [var.distribution_dir, "envoy_networking.sh"])
    destination = "/tmp/envoy_networking.sh"
  }
  provisioner "file" {
    source      = join("/", [var.distribution_dir, "hc.bash"])
    destination = "/tmp/hc.bash"
  }
  provisioner "file" {
    source      = join("/", [var.distribution_dir, "health.proto"])
    destination = "/tmp/health.proto"
  }
  provisioner "file" {
    source      = join("/", [var.distribution_dir, "/${var.service}.eif"])
    destination = "/tmp/server_enclave_image.eif"
  }
  provisioner "file" {
    source = join("/", [var.workspace, "production/packaging/aws/common/ami/vsockproxy.service"])
    destination = "/tmp/vsockproxy.service"
  }
  provisioner "file" {
    content     = <<EOF
#!/bin/bash
mkdir -p /etc/envoy
mv /tmp/bidding_auction_servers_descriptor_set.pb /etc/envoy/bidding_auction_servers_descriptor_set.pb
mv /tmp/envoy.yaml /etc/envoy/envoy.yaml
chmod 555 /etc/envoy/{envoy.yaml,bidding_auction_servers_descriptor_set.pb}

mv /tmp/vsockproxy.service /etc/systemd/system/vsockproxy.service
sudo mkdir -p /opt/privacysandbox
mv /tmp/proxy /opt/privacysandbox/proxy
mv /tmp/server_enclave_image.eif /opt/privacysandbox/server_enclave_image.eif
mv /tmp/otel_collector_config.yaml /opt/privacysandbox/otel_collector_config.yaml
mv /tmp/envoy_networking.sh /opt/privacysandbox/envoy_networking.sh
mv /tmp/hc.bash /opt/privacysandbox/hc.bash
mv /tmp/health.proto /opt/privacysandbox/health.proto
chmod 555 /opt/privacysandbox/{proxy,server_enclave_image.eif,otel_collector_config.yaml,envoy_networking.sh,hc.bash,health.proto}
EOF
    destination = "/tmp/setup_files"
  }
  # Create worker overrides directory and move override there;
  provisioner "shell" {
    inline = [
      "sudo bash /tmp/setup_files",
    ]
  }
  provisioner "shell" {
    script = join("/", [var.workspace, "production/packaging/aws/common/ami/setup.sh"])
  }
}
