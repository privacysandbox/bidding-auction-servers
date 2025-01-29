/**
 * Copyright 2023 Google LLC
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


resource "aws_cloudwatch_dashboard" "environment_dashboard" {
  dashboard_name = "${var.environment}-seller-metrics"

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
                      [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"request.count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "request.count [MEAN]"
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
                      [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"request.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "request.duration_ms [MEAN]"
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
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"request.failed_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.status_code')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "request.failed_count [MEAN]"
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
                     [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"request.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "request.size_bytes [MEAN]"
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
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"response.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "response.size_bytes [MEAN]"
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
            "y": 18,
            "x": 10,
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
            "y": 24,
            "x": 0,
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
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.thread.count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.label')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "system.thread.count [MEAN]"
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
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"initiated_request.count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.server name')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "initiated_request.count [MEAN]"
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
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.key_fetch.failure_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.label')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "system.key_fetch.failure_count [MEAN]"
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
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.key_fetch.num_keys_cached_after_recent_fetch\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.label')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "system.key_fetch.num_keys_cached_after_recent_fetch [MEAN]"
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
                    [ { "expression": "SEARCH(' deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"system.key_fetch.num_keys_parsed_on_recent_fetch\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.label')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "system.key_fetch.num_keys_parsed_on_recent_fetch [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.errors_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.error_code')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.errors_count [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"auction\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"auction.errors_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.error_code')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "auction.errors_count [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_auction.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_auction.duration_ms [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_auction.errors_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.status_code')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_auction.errors_count [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_auction.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_auction.size_bytes [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_bfe.errors_count_by_status\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.status_code')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_bfe.errors_count_by_status [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"initiated_request.to_kv.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "initiated_request.to_kv.duration_ms [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"initiated_request.to_kv.errors_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.status_code')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "initiated_request.to_kv.errors_count [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"initiated_request.to_kv.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "initiated_request.to_kv.size_bytes [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 66,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_response.to_auction.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_response.to_auction.size_bytes [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 72,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_response.to_kv.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_response.to_kv.size_bytes [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 72,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_bfe.count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.buyer')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_bfe.count [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 78,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_bfe.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.buyer')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_bfe.duration_ms [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 78,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_bfe.errors_count_by_buyer\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.buyer')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_bfe.errors_count_by_buyer [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 84,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_bfe.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.buyer')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_bfe.size_bytes [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 84,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_response.to_bfe.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.buyer')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_response.to_bfe.size_bytes [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 90,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"auction\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"udf_execution.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "udf_execution.duration_ms [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 90,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"auction\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"udf_execution.errors_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "udf_execution.errors_count [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 96,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"auction\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"auction.business_logic.bids_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "auction.business_logic.bids_count [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 96,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"auction\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"auction.business_logic.bid_rejected_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.seller_rejection_reason')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "auction.business_logic.bid_rejected_count [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 102,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"auction\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"auction.business_logic.bid_rejected_percent\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.seller_rejection_reason')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "auction.business_logic.bid_rejected_percent [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 102,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.business_logic.request_with_winner_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.business_logic.request_with_winner_count [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 108,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.business_logic.request_with_winner.duration_ms\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.business_logic.request_with_winner.duration_ms [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 108,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.protected_ciphertext.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.protected_ciphertext.size_bytes [MEAN]"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 114,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.auction_config.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.auction_config.size_bytes [MEAN]"
            }
        },
        {
            "type": "metric",
            "x": 0,
            "y": 120,
            "width": 20,
            "height": 6,
            "properties": {
                "sparkline": false,
                "view": "table",
                "metrics": [
                    [ { "expression": "SELECT MAX(\"system.bucket_fetch.blob_load_status\") FROM SCHEMA(auction, Noise,OTelLib,\"deployment.environment\",label,operator,region,\"service.instance.id\",\"service.name\",\"service.version\",\"telemetry.sdk.language\",\"telemetry.sdk.name\",\"telemetry.sdk.version\") WHERE \"deployment.environment\" = '${var.environment}' AND \"service.name\" = 'auction' GROUP BY label, \"service.instance.id\" ORDER BY MAX() DESC", "label": "Blob: ", "id": "q1" } ]
                ],
                "region": "${var.region}",
                "stat": "Average",
                "period": 300,
                "table": {
                    "summaryColumns": [
                        "MIN",
                        "MAX"
                    ],
                    "showTimeSeriesData": false
                },
                "title": "Latest Blob Load Status",
                "liveData": true
            }
        }
    ]
}
EOF
}
