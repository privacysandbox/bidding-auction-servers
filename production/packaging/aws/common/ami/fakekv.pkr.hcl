# Copyright 2024 Google LLC
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


# Packer variables
variable "aws_region" {
  type    = string
  default = "us-west-1"
}

variable "source_ami" {
  type        = string
  description = "The pre-existing AMI ID with Docker pre-installed, for detail build steps, see: https://docs.google.com/document/d/1yn5AvJMPPj5K6rrqZ7Urxa9mC6tNs47M7Q81bKS3uZw/edit?tab=t.0#heading=h.xuhpbm2cu9vj"
  default = "ami-031d3f5ab44d3da61"
}

variable "dir" {
  type    = string
}

variable "image_name" {
  type        = string
  description = "The name for the new AMI"
  default     = "aws-fakekv-image"
}

# Define the Packer source (Amazon EBS in this case)
source "amazon-ebs" "docker_image_update" {
  region          = var.aws_region
  instance_type   = "t2.micro"
  source_ami      = var.source_ami
  ssh_username    = "ec2-user"
  ami_name        = var.image_name
  ami_description = "An updated AMI with new Docker images"
  force_deregister  = true

  tags = {
    "Name"        = "DockerImageUpdate"
    "Environment" = "All"
  }
}

build {
  sources = ["source.amazon-ebs.docker_image_update"]

  # Upload the directory containing Dockerfile and build context
  provisioner "file" {
    source      = var.dir
    destination = "/home/ec2-user"
  }

  provisioner "shell" {
    inline = [
      "sudo systemctl start docker",
      "cd /home/ec2-user",

      # Build Docker image from local directory
      "docker build -t buyer-envoy:latest ./buyer-envoy",
      "docker build -t buyer-python:latest ./buyer-python",
      "docker build -t seller-envoy:latest ./seller-envoy",
      "docker build -t seller-python:latest ./seller-python",
    ]
  }
}
