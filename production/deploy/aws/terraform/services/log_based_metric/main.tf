/**
 * Copyright 2025 Google LLC
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

resource "aws_cloudwatch_log_metric_filter" "fatal_log" {
  name           = "fatal_log-${var.environment}"
  log_group_name = var.consented_log_group
  pattern        = "{ $.severity_text = \"FATAL\" }"

  metric_transformation {
    name      = "fatal_log-${var.environment}"
    namespace = "log_based_metric"
    value     = "1"
    unit      = "Count"

    dimensions = {
      InstanceId = "$.resource.['service.instance.id']"
      Service    = "$.resource.['service.name']"
      Env        = "$.resource.['deployment.environment']"
    }
  }
}

resource "aws_cloudwatch_log_metric_filter" "error_log" {
  name           = "error_log-${var.environment}"
  log_group_name = var.consented_log_group
  pattern        = "{ $.severity_text = \"ERROR\" }"

  metric_transformation {
    name      = "error_log-${var.environment}"
    namespace = "log_based_metric"
    value     = "1"
    unit      = "Count"

    dimensions = {
      InstanceId = "$.resource.['service.instance.id']"
      Service    = "$.resource.['service.name']"
      Env        = "$.resource.['deployment.environment']"
    }
  }
}

resource "aws_cloudwatch_query_definition" "fatal_log" {
  name            = "fatal_log-${var.environment}"
  log_group_names = [var.consented_log_group]

  query_string = <<EOF
fields @timestamp, resource.deployment.environment, resource.service.name, @message
| filter resource.deployment.environment = "${var.environment}" and severity_text = "FATAL"
EOF
}

resource "aws_cloudwatch_query_definition" "error_log" {
  name            = "error_log-${var.environment}"
  log_group_names = [var.consented_log_group]

  query_string = <<EOF
fields @timestamp, resource.deployment.environment, resource.service.name, @message
| filter resource.deployment.environment = "${var.environment}" and severity_text = "ERROR"
EOF
}

locals {
  services = ["sfe", "bfe", "auction", "bidding"]
}

resource "aws_cloudwatch_metric_alarm" "metric_alarm" {
  for_each            = toset(local.services)
  alarm_name          = "High Request Failure Rate-${each.key}-${var.environment}"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 1
  datapoints_to_alarm = 1
  threshold           = var.request_fail_alert_threshold
  treat_missing_data  = "missing"
  actions_enabled     = true

  alarm_description = "Request Failure Rate > ${var.request_fail_alert_threshold} for ${each.key}:${var.environment}"

  metric_query {
    id          = "m3"
    expression  = "m2 / m1"
    label       = "Failure Ratio"
    return_data = true
  }

  metric_query {
    id          = "m2"
    return_data = false
    metric {
      namespace   = each.key
      metric_name = "request.failed_count"
      dimensions = {
        OTelLib                  = "${each.key}"
        "deployment.environment" = "${var.environment}"
      }
      period = 60
      stat   = "Average"
    }
  }

  metric_query {
    id          = "m1"
    return_data = false
    metric {
      namespace   = each.key
      metric_name = "request.count"
      dimensions = {
        OTelLib                  = "${each.key}"
        "deployment.environment" = "${var.environment}"
      }
      period = 60
      stat   = "Average"
    }
  }
}
