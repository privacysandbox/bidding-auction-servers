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

"""Tests for estimator module."""

import unittest
from unittest.mock import MagicMock
from estimator import EstimatorTables, CostEstimator
from sql_expr import SqlExecutor


class EstimatorTablesTest(unittest.TestCase):
    """Tests for EstimatorTables."""

    def setUp(self):
        """Setup method to create mock objects before each test."""
        self.mock_sql_executor = MagicMock()
        self.mock_tables = EstimatorTables(self.mock_sql_executor)
        self.real_sql_executor = SqlExecutor()
        self.real_tables = EstimatorTables(self.real_sql_executor)

    def test_add_sku_data(self):
        """Test adding sku data."""
        self.mock_tables.add_sku_data(
            "aws1", "us-west-1", "Network Egress", "sku1", 10.5, "HR"
        )
        self.mock_sql_executor.execute_in_db.assert_called_with(
            "INSERT INTO SkuData(platform, region, description, sku_id, unit_cost, cost_basis) "
            "values ('aws1', 'us-west-1', 'Network Egress', 'sku1', 10.5, 'HR')"
        )
        self.real_tables.add_sku_data(
            "aws1", "us-west-1", "Network Egress", "sku1", 10.5, "HR"
        )
        result = self.real_sql_executor.execute_in_db(
            "select platform, region, description, sku_id, unit_cost, cost_basis "
            "from SkuData where sku_id = 'sku1'"
        )
        self.assertEqual(
            result, [("aws1", "us-west-1", "Network Egress", "sku1", 10.5, "HR")]
        )

    def test_add_usage_estimate(self):
        """Test adding usage estimate."""
        self.mock_tables.add_usage_estimate("Network Egress", 100.0, "Seller-Compute")
        self.mock_sql_executor.execute_in_db.assert_called_with(
            "INSERT INTO SkuEstimates(description, estimate, category) "
            "values ('Network Egress', 100.0, 'Seller-Compute')"
        )
        self.real_tables.add_usage_estimate("Network Egress", 100.0, "Seller-Compute")
        result = self.real_sql_executor.execute_in_db(
            "select description, estimate, category from SkuEstimates "
            "where category = 'Seller-Compute'"
        )
        self.assertEqual(result, [("Network Egress", 100.0, "Seller-Compute")])

    def test_load_sku_data(self):
        """Test loading sku data from json."""
        sku_json = {
            "vendor1": [
                {
                    "region": "us-west-1",
                    "description": "Network Egress",
                    "sku_id": "sku1",
                    "unit_cost": 10.5,
                    "cost_basis": "HR",
                },
            ]
        }
        self.mock_tables.load_sku_data(sku_json)
        self.mock_sql_executor.execute_in_db.assert_called_with(
            "INSERT INTO SkuData(platform, region, description, sku_id, unit_cost, cost_basis) "
            "values ('vendor1', 'us-west-1', 'Network Egress', 'sku1', 10.5, 'HR')"
        )


class CostEstimatorTest(unittest.TestCase):
    """Tests the CostEstimator"""

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

        self.cost_yaml = {
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

    def test_get_metrics(self):
        self.cost_estimator.run()
        expected_metrics = {
            "sfe:request.count": 500,
            "sfe:request.size_bytes": 809600,
            "sfe:system.cpu.count": 2,
            "test.duration": 3,
        }
        self.assertEqual(self.cost_estimator.get_metrics(), expected_metrics)

    def test_estimation(self):
        """Test estimates generated by the CostEstimator.run() function."""
        self.cost_estimator.run()

        # check usage estimates
        result = self.sql_executor.execute_in_db(
            "select sku_description, estimated_usage from CostEstimates order by sku_description"
        )
        self.assertEqual(
            result,
            [
                ("compute-cpu-sku", 12.0),
                ("compute-ram-sku", 48.0),
                ("network-lb-data-processing-sku", 809600.0),
                ("network-lb-per-hour-sku", 3.0),
                ("unknown-sku", 2428800.0),
            ],
        )

        # check cost
        result = self.sql_executor.execute_in_db(
            "select sku_description, estimated_cost from CostEstimates order by sku_description"
        )
        self.assertEqual(
            result,
            [
                ("compute-cpu-sku", 24.0),
                ("compute-ram-sku", 144.0),
                ("network-lb-data-processing-sku", 40480.0),
                ("network-lb-per-hour-sku", 21.0),
                ("unknown-sku", None),
            ],
        )

        # number of requests is 500, so conversion to million is 1M/500
        convert_to_million = 1000000 / 500

        # check usage per million
        result = self.sql_executor.execute_in_db(
            "select sku_description, estimated_usage_per_million "
            "from CostEstimates order by sku_description"
        )
        self.assertEqual(
            result,
            [
                ("compute-cpu-sku", 12.0 * convert_to_million),
                ("compute-ram-sku", 48.0 * convert_to_million),
                ("network-lb-data-processing-sku", 809600.0 * convert_to_million),
                ("network-lb-per-hour-sku", 3.0 * convert_to_million),
                ("unknown-sku", 2428800 * convert_to_million),
            ],
        )

        # check usage per million
        result = self.sql_executor.execute_in_db(
            "select sku_description, estimated_cost_per_million "
            "from CostEstimates order by sku_description"
        )
        self.assertEqual(
            result,
            [
                ("compute-cpu-sku", 24.0 * convert_to_million),
                ("compute-ram-sku", 144.0 * convert_to_million),
                ("network-lb-data-processing-sku", 40480.0 * convert_to_million),
                ("network-lb-per-hour-sku", 21.0 * convert_to_million),
                ("unknown-sku", None),
            ],
        )

    def test_attributes(self):
        """Test attributes generated by the CostEstimator.run() function"""
        self.cost_estimator.run()

        # check usage estimates
        result = self.sql_executor.execute_in_db(
            "select sku_description, sku_id, category from CostEstimates order by sku_description"
        )
        self.assertEqual(
            result,
            [
                ("compute-cpu-sku", "compute-cpu-sku-id", "sfe-compute"),
                ("compute-ram-sku", "compute-ram-sku-id", "sfe-compute"),
                (
                    "network-lb-data-processing-sku",
                    "network-lb-data-processing-sku-id",
                    "sfe-network",
                ),
                (
                    "network-lb-per-hour-sku",
                    "network-lb-per-hour-sku-id",
                    "sfe-network",
                ),
                ("unknown-sku", None, "unknown"),
            ],
        )


if __name__ == "__main__":
    unittest.main()
