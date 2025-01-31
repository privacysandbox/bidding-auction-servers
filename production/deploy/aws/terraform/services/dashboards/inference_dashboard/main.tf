# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

resource "aws_cloudwatch_dashboard" "environment_dashboard" {
  dashboard_name = "${var.environment}-inference-metrics"

  # https://docs.aws.amazon.com/AmazonCloudWatch/latest/APIReference/CloudWatch-Dashboard-Body-Structure.html
  dashboard_body = <<EOF
{
    "widgets": [
         {
            "height": 6,
            "width": 10,
            "y": 0,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.request.count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.request.count [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 0,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.request.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.request.duration_ms [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 6,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.request.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.request.size_bytes [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 6,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.response.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.response.size_bytes [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 12,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.request.failed_count_by_status\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.status_code')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.request.failed_count_by_status [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 12,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.cpu.percent\" label=(\"total utilization\" OR \"main process utilization\")', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.label')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "system.cpu.percent [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 18,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.memory.usage_kb\" label=\"main process\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "system.memory.usage_kb for main process [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 18,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.memory.usage_kb\" label=\"MemAvailable:\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "system.memory.usage_kb for MemAvailable [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 24,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.thread.count\" label=\"main process thread count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.label')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "system.thread.count for main process [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 24,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.thread.count\" label=\"inference process thread count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.label')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "system.thread.count for inference process [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 30,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.cpu.percent\" label=\"total cpu cores\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "system.cpu.total_cores [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 66,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.request.count_by_model\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.model')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.request.count_by_model [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 30,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.request.duration_ms_by_model\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.model')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.request.duration_ms_by_model [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 36,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.request.failed_count_by_model\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.model')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.request.failed_count_by_model [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 36,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.request.batch_count_by_model\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.model')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.request.batch_count_by_model [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 42,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.errors_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.errors_count [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 42,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.cloud_fetch.failed_count_by_status\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.status_code')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.cloud_fetch.failed_count_by_status [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 48,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.model_registration.recent_success_by_model\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.label')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.model_registration.recent_success_by_model [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 48,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.model_registration.failed_count_by_status\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.status_code')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.model_registration.failed_count_by_status [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 54,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.model_registration.recent_failure_by_model\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.label')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.model_registration.recent_failure_by_model [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 54,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.model_registration.available_models\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.label')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.model_registration.available_models [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 60,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"inference.cloud_fetch.success_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "inference.cloud_fetch.success_count [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 60,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"bidding.inference.request.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
                ],
                "timezone": "UTC",
                "region": "${var.region}",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "bidding.inference.request.duration_ms [MEAN]"
            }
        }
    ]
}
EOF
}
