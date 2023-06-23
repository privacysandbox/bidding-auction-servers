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
            "y": 1,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                     [ "sfe", "kTotalRequestCounter", "OTelLib", "sfe", "deployment.environment", "${var.environment}", { "id": "m2" } ]
                ],
                "timezone": "UTC",
                "region": "us-west-1",
                "view": "timeSeries",
                "stacked": false,
                "stat": "Average",
                "period": 300,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "label": "",
                        "showUnits": false
                    },
                    "right": {
                        "min": 0
                    }
                },
                "title": "kTotalRequestCounter",
                "legend": {
                    "position": "hidden"
                }
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 1,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ "sfe", "kServerTotalTimeMs", "OTelLib", "sfe", "deployment.environment", "${var.environment}", { "id": "m2" } ]
                ],
                "timezone": "UTC",
                "region": "us-west-1",
                "view": "timeSeries",
                "stacked": false,
                "stat": "Average",
                "period": 300,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false,
                        "label": "ns"
                    },
                    "right": {
                        "min": 0
                    }
                },
                "title": "kServerTotalTimeMs",
                "legend": {
                    "position": "hidden"
                }
            }
        },
        {
            "height": 1,
            "width": 20,
            "y": 0,
            "x": 0,
            "type": "text",
            "properties": {
                "markdown": "## Seller Front End"
            }
        },
        {
            "height": 1,
            "width": 20,
            "y": 7,
            "x": 0,
            "type": "text",
            "properties": {
                "markdown": "## Auction"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 8,
            "x": 0,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ "auction", "kTotalRequestCounter", "OTelLib", "auction", "deployment.environment", "${var.environment}", { "id": "m2" } ]
                ],
                "timezone": "UTC",
                "region": "us-west-1",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "showUnits": false
                    }
                },
                "title": "kTotalRequestCounter",
                "period": 300,
                "legend": {
                    "position": "hidden"
                },
                "stat": "Average"
            }
        },
        {
            "height": 6,
            "width": 10,
            "y": 8,
            "x": 10,
            "type": "metric",
            "properties": {
                "metrics": [
                    [ "auction", "kServerTotalTimeMs", "OTelLib", "auction", "deployment.environment", "${var.environment}", { "id": "m2" } ]
                ],
                "timezone": "UTC",
                "region": "us-west-1",
                "view": "timeSeries",
                "stacked": false,
                "yAxis": {
                    "left": {
                        "min": 0,
                        "label": "ns",
                        "showUnits": false
                    }
                },
                "title": "kServerTotalTimeMs",
                "period": 300,
                "stat": "Average",
                "legend": {
                    "position": "hidden"
                }
            }
        }
    ]
}
EOF
}
