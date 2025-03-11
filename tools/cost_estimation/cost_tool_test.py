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

"""Tests for cost_tool."""

import unittest
from unittest.mock import patch, mock_open, MagicMock
from cost_tool import CostTool
from render import ADDITIONAL_INFO_MESSAGE


class CostToolTest(unittest.TestCase):
    """Tests cases for CostTool."""

    def setUp(self):
        """Setup method to create mock objects before each test."""
        self.sku_json = {
            "aws": [
                {
                    "region": "us-west-1",
                    "description": "compute-cpu-sku",
                    "sku_id": "compute-cpu-sku-id",
                    "unit_cost": 2,
                    "cost_basis": "HR",
                },
                {
                    "region": "us-west-1",
                    "description": "compute-ram-sku",
                    "sku_id": "compute-ram-sku-id",
                    "unit_cost": 3,
                    "cost_basis": "HR",
                },
                {
                    "region": "us-west-1",
                    "description": "network-lb-data-processing-sku",
                    "sku_id": "network-lb-data-processing-sku-id",
                    "unit_cost": 0.05,
                    "cost_basis": "GB",
                },
                {
                    "region": "us-west-1",
                    "description": "network-lb-per-hour-sku",
                    "sku_id": "network-lb-per-hour-sku-id",
                    "unit_cost": 7,
                    "cost_basis": "HR",
                },
            ]
        }
        self.cost_yaml = [
            {
                "cost_model_metadata": {
                    "region": "us-west-1",
                    "num_requests_metric": "sfe:request.count",
                    "name": "basic_model",
                    "vendor": "aws",
                },
                "download_metrics": [
                    {
                        "metric": "sfe:request.count",
                        "service": ["sfe"],
                        "aggregation": "Sum",
                    },
                    {
                        "metric": "sfe:request.size_bytes",
                        "service": ["sfe"],
                        "aggregation": "Sum",
                    },
                    {
                        "metric": "sfe:system.cpu.count",
                        "service": ["sfe"],
                        "aggregation": "Sum",
                    },
                ],
                "defined_values": {
                    "sfe_core_hours": "test.duration * sfe:system.cpu.count"
                },
                "usage_estimations": {
                    "sfe-compute": {
                        "compute-cpu-sku": "sfe_core_hours * sfe:system.cpu.count",
                        "compute-ram-sku": "sfe_core_hours * sfe:system.cpu.count * 4",
                    },
                    "sfe-network": {
                        "network-lb-data-processing-sku": "sfe:request.size_bytes",
                        "network-lb-per-hour-sku": "test.duration",
                    },
                    "unknown": {
                        "unknown-sku": "sfe:request.size_bytes * test.duration"
                    },
                },
            }
        ]

        self.metrics = {
            "sfe:request.count": 500,
            "sfe:request.size_bytes": 809600,
            "sfe:system.cpu.count": 2,
            "test.duration": 3,
        }
        self.args = MagicMock()
        self.metrics_loader = MagicMock()
        self.metrics_loader.get_metrics.return_value = self.metrics
        self.cost_tool = CostTool()

    @patch("builtins.open", new_callable=mock_open, read_data="request.count, 123")
    def test_load_metrics_from_file(self, _):
        self.args.metrics_file = "metrics.csv"
        self.args.cost_model = "basic_model"
        metrics = self.cost_tool.load_metrics(
            self.args, self.cost_yaml[0], "basic_model"
        )
        self.assertEqual(metrics, {"cost_model": "basic_model", "request.count": "123"})

    def test_run_cost_tool_with_json(self):
        """Test get_cost_model_data with a valid model name."""
        actual = (
            self.cost_tool.run_internal(
                cost_yaml=self.cost_yaml[0],
                metrics=self.metrics,
                sku_json=self.sku_json,
            )
            .replace("\n\r", "\n")
            .replace("\r\n", "\n")
        )
        expected = (
            ADDITIONAL_INFO_MESSAGE
            + """\n\nSummary
Category,Estimated Cost Per Million Queries
sfe-compute,$336000.00
sfe-network,$81002000.00
unknown,$0.00
Total,$81338000.00

sfe-compute
Sku name,Sku-Id,Unit Cost,Cost Basis,Estimated Units,Estimated Cost,Estimated Units Per Million Queries,Estimated Cost Per Million Queries
compute-cpu-sku,compute-cpu-sku-id,$2.00,HR,12.00,$24.00,24000.00,$48000.00
compute-ram-sku,compute-ram-sku-id,$3.00,HR,48.00,$144.00,96000.00,$288000.00
Total,,,,,$168.00,,$336000.00

sfe-network
Sku name,Sku-Id,Unit Cost,Cost Basis,Estimated Units,Estimated Cost,Estimated Units Per Million Queries,Estimated Cost Per Million Queries
network-lb-data-processing-sku,network-lb-data-processing-sku-id,$0.05,GB,809600.00,$40480.00,1619200000.00,$80960000.00
network-lb-per-hour-sku,network-lb-per-hour-sku-id,$7.00,HR,3.00,$21.00,6000.00,$42000.00
Total,,,,,$40501.00,,$81002000.00

unknown
Sku name,Sku-Id,Unit Cost,Cost Basis,Estimated Units,Estimated Cost,Estimated Units Per Million Queries,Estimated Cost Per Million Queries
unknown-sku,,$0.00,,2428800.00,$0.00,4857600000.00,$0.00
Total,,,,,$0.00,,$0.00


Metric, Value
sfe:request.count, 500
sfe:request.size_bytes, 809600
sfe:system.cpu.count, 2
test.duration, 3
""".replace(
                "\n\r", "\n"
            ).replace(
                "\r\n", "\n"
            )
        )
        self.assertEqual(actual, expected)

    def test_get_cost_model_data(self):
        """Tests getting the cost model from the cost yaml"""
        model0 = {
            "cost_model_metadata": {
                "region": "us-west-1",
                "num_requests_metric": "sfe:request.count",
                "vendor": "aws",
                "name": "basic_model",
            }
        }
        model1 = {
            "cost_model_metadata": {
                "region": "us-west-1",
                "num_requests_metric": "sfe:request.count",
                "vendor": "gcp",
                "name": "basic_model1",
            }
        }
        yaml = [model1, model0]
        actual0 = self.cost_tool.get_cost_model_data("basic_model", yaml)
        self.assertEqual(actual0, model0)
        actual1 = self.cost_tool.get_cost_model_data("basic_model1", yaml)
        self.assertEqual(actual1, model1)
        with self.assertRaises(Exception):  # Expect an exception for unknown model
            self.cost_tool.get_cost_model_data("unknown-model", yaml)

    @patch("builtins.open", new_callable=mock_open, read_data="request.count, 123")
    def test_file__no_write(self, mock_open_object):
        """Tests that file is not written to when using metrics loaded from a file."""
        self.args.metrics_file = "metrics.csv"
        self.args.cost_model = "basic_model"
        self.cost_tool.load_metrics(self.args, self.cost_yaml[0], "basic_model")
        mock_open_object.assert_called_once()
        mock_open_object().write.assert_not_called()

    @patch("builtins.open", new_callable=mock_open)
    @patch("boto3.client")
    def test_file_write(self, mock_boto, mock_open_object):
        """Tests that a file gets written to when loading metrics from cloud."""
        self.args.cost_model = "basic_model"
        self.args.gcp_metrics_download = None
        self.args.metrics_file = None
        self.args.save_downloaded_metrics = None
        self.args.aws_metrics_download = [
            "env",
            "2024-12-12T08:00:00-07:00",
            "2024-12-12T12:00:00-07:00",
        ]

        mock_boto.get_metrics_statistics.return_value = {"Datapoints": []}
        self.cost_tool.load_metrics(self.args, self.cost_yaml[0], "basic_model")
        mock_open_object.assert_called()
        mock_open_object().write.assert_called_once()


if __name__ == "__main__":
    unittest.main()
