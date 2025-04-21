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

resource "google_logging_metric" "error_log_metric" {
  name   = "error_log-${var.environment}"
  filter = "resource.type=\"generic_task\" AND severity=ERROR"
  metric_descriptor {
    metric_kind = "DELTA"
    value_type  = "INT64"
    labels {
      key         = "message"
      value_type  = "STRING"
      description = "The extracted error message"
    }
  }
  label_extractors = {
    message = "REGEXP_EXTRACT(textPayload, \"[E\\\\d .:]*(.*)\")"
  }
}

resource "google_logging_metric" "fatal_log_metric" {
  name   = "fatal_log-${var.environment}"
  filter = "resource.type=\"generic_task\" AND severity=CRITICAL"
  metric_descriptor {
    metric_kind = "DELTA"
    value_type  = "INT64"
  }
}

resource "google_logging_metric" "crash_log_metric" {
  name   = "vm_crash_log-${var.environment}"
  filter = "jsonPayload.MESSAGE=\"workload task ended and returned non-zero\" AND severity=ERROR"
  metric_descriptor {
    metric_kind = "DELTA"
    value_type  = "INT64"
    labels {
      key         = "host"
      value_type  = "STRING"
      description = "The hostname of the crash"
    }
  }
  label_extractors = {
    host = "EXTRACT(jsonPayload._HOSTNAME)"
  }
}

resource "google_monitoring_alert_policy" "alert_policy" {
  display_name = "High Request Failure Rate-${var.environment}"
  combiner     = "OR"
  conditions {
    display_name = "Request Failure Rate > ${var.request_fail_alert_threshold}"
    condition_prometheus_query_language {
      query               = <<EOT
          sum by(deployment_environment, service_name) (
            rate(workload_googleapis_com:request_failed_count{
              monitored_resource="generic_task",
              deployment_environment=~".*${var.environment}.*"
            }[1m])
          )
          /
          sum by(deployment_environment, service_name) (
            rate(workload_googleapis_com:request_count{
              monitored_resource="generic_task",
              deployment_environment=~".*${var.environment}.*"
            }[1m])
          ) > ${var.request_fail_alert_threshold}
        EOT
      duration            = "120s"
      evaluation_interval = "60s"
    }
  }

  alert_strategy {
    auto_close = "1800s"
  }
}
