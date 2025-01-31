/**
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

####################################################
# Create EC2 instance profile.
####################################################
data "aws_iam_policy_document" "ec2_assume_role_policy" {
  statement {
    actions = [
      "sts:AssumeRole"
    ]
    principals {
      identifiers = [
        "ec2.amazonaws.com"
      ]
      type = "Service"
    }
  }
}

resource "aws_iam_role" "instance_role" {
  name               = format("%s-%s-%s-InstanceRole", var.operator, var.environment, var.region)
  assume_role_policy = data.aws_iam_policy_document.ec2_assume_role_policy.json

  tags = {
    Name        = format("%s-%s-%s-InstanceRole", var.operator, var.environment, var.region)
    operator    = var.operator
    environment = var.environment
    region      = var.region
  }
}

resource "aws_iam_instance_profile" "instance_profile" {
  name = format("%s-%s-%s-InstanceProfile", var.operator, var.environment, var.region)
  role = aws_iam_role.instance_role.name

  tags = {
    Name        = format("%s-%s-%s-InstanceProfile", var.operator, var.environment, var.region)
    operator    = var.operator
    environment = var.environment
    region      = var.region
  }
}
