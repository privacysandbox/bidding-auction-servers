# Copyright 2025 Google LLC
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
  dashboard_name = "${var.environment}-roma-metrics"

  # https://docs.aws.amazon.com/AmazonCloudWatch/latest/APIReference/CloudWatch-Dashboard-Body-Structure.html
  dashboard_body = <<EOF
{
    "widgets": [
        {
            "height": 6,
            "width": 10,
            "x": 0,
            "y": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"roma.execution.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "roma.execution.duration_ms [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "x": 10,
            "y": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"roma.execution.code_run.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "roma.execution.code_run.duration_ms [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "x": 0,
            "y": 6,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"roma.execution.json_input_parsing.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "roma.execution.json_input_parsing.duration_ms [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"roma.execution.js_engine_handler_call.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "roma.execution.js_engine_handler_call.duration_ms [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "x": 0,
            "y": 12,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"roma.execution.queue_fullness_ratio\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "roma.execution.queue_fullness_ratio [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"roma.execution.active_worker_ratio\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "roma.execution.active_worker_ratio [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "x": 0,
            "y": 18,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"bidding\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"udf_execution.dispatcher_initialization.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "udf_execution.dispatcher_initialization.duration_ms [MEAN]"
            }
        }
    ]
}
EOF
}
