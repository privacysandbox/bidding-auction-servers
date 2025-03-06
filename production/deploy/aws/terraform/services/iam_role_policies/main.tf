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
    resources = var.coordinator_role_arns
  }
  statement {
    sid = "AllowInstancesToRegisterInstance"
    actions = [
      "servicediscovery:RegisterInstance",
      "route53:CreateHealthCheck",
      "route53:GetHealthCheck",
      "route53:UpdateHealthCheck",
      "route53:ChangeResourceRecordSets",
      "ec2:DescribeInstances",
      "s3:ListBucket",
      "s3:GetBucketLocation",
      "s3:GetObject",
    ]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid = "AllowInstancesToSetInstanceHealthForASGandCloudMap"
    actions = [
      "autoscaling:SetInstanceHealth",
      "servicediscovery:UpdateInstanceCustomHealthStatus",
      "servicediscovery:DeregisterInstance",
    ]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid = "AllowTLSAuth"
    actions = [
      "acm:DescribeCertificate",
      "acm-pca:DescribeCertificateAuthority",
      "acm:ExportCertificate",
      "acm-pca:GetCertificateAuthorityCertificate"
    ]
    effect    = "Allow"
    resources = ["*"]
  }
  statement {
    sid    = "RunCloudUnMap"
    effect = "Allow"
    actions = [
      "ec2:DescribeInstances",
      "servicediscovery:ListInstances",
      "servicediscovery:DeregisterInstance",
      "route53:GetHealthCheck",
      "route53:DeleteHealthCheck",
      "route53:UpdateHealthCheck",
    ]
    resources = ["*"]
  }
  statement {
    sid       = "UpdateDnsWhileDeregisteringServiceInstancesForCloudUnMap"
    effect    = "Allow"
    actions   = ["route53:ChangeResourceRecordSets"]
    resources = ["*"]
  }
  statement {
    sid       = "AllowWriteS3"
    effect    = "Allow"
    actions   = ["s3:PutObject"]
    resources = ["*"]
  }
}

resource "aws_iam_policy" "instance_policy" {
  name   = format("%s-%s-%s-InstancePolicy", var.operator, var.environment, var.region)
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
