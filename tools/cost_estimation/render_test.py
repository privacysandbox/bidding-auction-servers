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
"""Tests for the render module."""

import unittest

from render import CsvRenderer
from sql_expr import SqlExecutor
from estimator import CostEstimator
from render import ADDITIONAL_INFO_MESSAGE


class CsvRendererTest(unittest.TestCase):
    """Tests for CsvRenderer."""

    def setUp(self):
        self.sql_executor = SqlExecutor()
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
        self.cost_yaml = self.cost_yaml = {
            "cost_model_metadata": {
                "region": "us-west-1",
                "num_requests_metric": "sfe:request.count",
            },
            "download_metrics": [
                "sfe:request.count",
                "sfe:request.size_bytes",
                "sfe:system.cpu.count",
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
                "unknown": {"unknown-sku": "sfe:request.size_bytes * test.duration"},
            },
        }

        self.metrics = {
            "sfe:request.count": 500,
            "sfe:request.size_bytes": 809600,
            "sfe:system.cpu.count": 2,
            "test.duration": 3,
        }
        self.cost_estimator = CostEstimator(
            self.sql_executor, self.sku_json, self.cost_yaml, self.metrics
        )
        self.cost_estimator.run()

    def test_render_no_template(self):
        """Test rendering without a template from cost_yaml."""
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

        self.renderer = CsvRenderer(self.sql_executor, self.cost_yaml, self.metrics)
        actual = self.renderer.render().replace("\n\r", "\n").replace("\r\n", "\n")
        self.assertEqual(actual, expected)

    def test_render_with_template(self):
        """Test rendering with a template from cost_yaml."""
        self.cost_yaml["results_template"] = (
            "$Summary\n$Metrics\n$unknown\n$sfe-compute\n$sfe-network"
        )
        expected = (
            ADDITIONAL_INFO_MESSAGE
            + """\n\nSummary
Category,Estimated Cost Per Million Queries
sfe-compute,$336000.00
sfe-network,$81002000.00
unknown,$0.00
Total,$81338000.00

Metric, Value
sfe:request.count, 500
sfe:request.size_bytes, 809600
sfe:system.cpu.count, 2
test.duration, 3

unknown
Sku name,Sku-Id,Unit Cost,Cost Basis,Estimated Units,Estimated Cost,Estimated Units Per Million Queries,Estimated Cost Per Million Queries
unknown-sku,,$0.00,,2428800.00,$0.00,4857600000.00,$0.00
Total,,,,,$0.00,,$0.00

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
""".replace(
                "\n\r", "\n"
            ).replace(
                "\r\n", "\n"
            )
        )

        self.renderer = CsvRenderer(self.sql_executor, self.cost_yaml, self.metrics)
        actual = self.renderer.render().replace("\n\r", "\n").replace("\r\n", "\n")
        self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
