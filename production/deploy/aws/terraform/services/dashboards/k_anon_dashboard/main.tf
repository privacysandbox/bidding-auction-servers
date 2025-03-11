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
  dashboard_name = "${var.environment}-k-anon-metrics"

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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_k_anon.duration_ms\" ', 'Average', 1)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_k_anon.duration_ms [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.k_anon_query.duration_ms\" ', 'Average', 1)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.k_anon_query.overall_duration_ms [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_k_anon.errors_count\" ', 'Average', 1)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_k_anon.errors_count_by_status [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_request.to_k_anon.size_bytes\" ', 'Average', 1)", "id": "e1", "label": "$${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_request.to_k_anon.size_bytes [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.initiated_response.from_k_anon.size_bytes\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.status_code')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.initiated_response.from_k_anon.size_bytes [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.k_anon.hash_count\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.model')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.k_anon.hash_count [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.k_anon_cache.overall_hit_percentage\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.model')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.k_anon_cache.overall_hit_percentage [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.k_anon_cache.hit_percentage\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.model')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.k_anon_cache.hit_percentage [MEAN]"
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
                    [ { "expression": "SEARCH(' service.name=\"sfe\" deployment.environment=\"${var.environment}\" Noise=(\"Raw\" OR \"Noised\") MetricName=\"sfe.non_k_anon_cache.hit_percentage\" ', 'Average', 60)", "id": "e1", "label": "$${PROP('Dim.model')} $${PROP('Dim.service.name')} $${PROP('Dim.deployment.environment')} $${PROP('Dim.Noise')} $${PROP('Dim.service.instance.id')}" } ]
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
                "title": "sfe.non_k_anon_cache.hit_percentage [MEAN]"
            }
        }
    ]
}
EOF
}
