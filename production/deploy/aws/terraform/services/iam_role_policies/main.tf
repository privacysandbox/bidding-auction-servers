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

data "aws_iam_policy_document" "instance_policy_doc" {
  statement {
    sid       = "AllowInstancesToReadTags"
    actions   = ["ec2:Describe*"]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid       = "AllowInstancesToCompleteLifecycleAction"
    actions   = ["autoscaling:CompleteLifecycleAction"]
    effect    = "Allow"
    resources = var.autoscaling_group_arns
  }
  statement {
    sid       = "AllowInstancesToRecordLifecycleActionHeartbeat"
    actions   = ["autoscaling:RecordLifecycleActionHeartbeat"]
    effect    = "Allow"
    resources = var.autoscaling_group_arns
  }
  statement {
    sid       = "AllowInstancesToDescribeAutoScalingInstances"
    actions   = ["autoscaling:DescribeAutoScalingInstances"]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid       = "AllowInstancesToReadParameters"
    actions   = ["ssm:GetParameter"]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid = "AllowOtelPutMetricData"
    actions = [
      "cloudwatch:PutMetricData",
      "logs:CreateLogGroup",
      "logs:CreateLogStream",
      "logs:PutLogEvents",
    ]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid = "AllowOtelPutTraces"
    actions = [
      "xray:PutTraceSegments",
      "xray:PutTelemetryRecords",
      "xray:GetSamplingRules",
      "xray:GetSamplingTargets",
      "xray:GetSamplingStatisticSummaries",
    ]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid = "AllowOtelPrometheusRemoteWrite"
    actions = [
      "aps:RemoteWrite",
    ]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid       = "AllowInstancesToAssumeRole"
    actions   = ["sts:AssumeRole"]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    actions = [
      "s3:ListBucket",
      "s3:GetBucketLocation",
      "s3:GetObject",
    ]
    effect    = "Allow"
    resources = ["*"]
  }
}

resource "aws_iam_policy" "instance_policy" {
  name   = format("%s-%s-InstancePolicy", var.operator, var.environment)
  policy = data.aws_iam_policy_document.instance_policy_doc.json
}

resource "aws_iam_role_policy_attachment" "instance_role_policy_attachment" {
  policy_arn = aws_iam_policy.instance_policy.arn
  role       = var.server_instance_role_name
}

resource "aws_iam_role_policy_attachment" "ssm_instance_role_attachment" {
  role       = var.server_instance_role_name
  policy_arn = "arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore"
}

resource "aws_iam_role_policy_attachment" "cloudwatch_instance_role_attachment" {
  role       = var.server_instance_role_name
  policy_arn = "arn:aws:iam::aws:policy/CloudWatchAgentServerPolicy"
}

# Set up policies for using EC2 instance connect.
data "aws_iam_policy_document" "ssh_instance_policy_doc" {
  statement {
    sid       = "AllowSSHInstanceToSendSSHPublicKey"
    actions   = ["ec2-instance-connect:SendSSHPublicKey"]
    resources = ["arn:aws:ec2:*:*:instance/*"]
    condition {
      test     = "StringEquals"
      variable = "aws:ResourceTag/environment"
      values   = [var.environment]
    }
    condition {
      test     = "StringEquals"
      variable = "ec2:osuser"
      values   = ["ec2-user"]
    }
  }
  statement {
    sid       = "AllowSSHInstanceToDescribeInstances"
    actions   = ["ec2:DescribeInstances"]
    resources = ["*"]
  }
}

resource "aws_iam_policy" "ssh_instance_policy" {
  name   = format("%s-%s-sshInstancePolicy", var.operator, var.environment)
  policy = data.aws_iam_policy_document.ssh_instance_policy_doc.json
}

resource "aws_iam_role_policy_attachment" "ssh_instance_access" {
  policy_arn = aws_iam_policy.ssh_instance_policy.arn
  role       = var.ssh_instance_role_name
}
