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

"""Classes for estimating costs."""

import logging
from sql_expr import SqlExecutor, ExpressionEvaluator
from utils import read_from_yaml_or_die

logger = logging.getLogger(__name__)


class EstimatorTables:
    """Stores estimated and sku data"""

    def __init__(self, sql_executor: SqlExecutor):
        self._sql_executor = sql_executor
        self._sql_executor.execute_in_db(
            "CREATE TABLE SkuData(platform STRING, "
            "region STRING, description STRING, "
            "sku_id STRING, "
            "unit_cost FLOAT, "
            "cost_basis STRING)"
        )
        self._sql_executor.execute_in_db(
            "CREATE TABLE SkuEstimates(description STRING, "
            "estimate FLOAT, "
            "category STRING)"
        )

    def load_sku_data(self, sku_json: dict):
        """Load sku data from the given json into the SkuData table."""
        for vendor, vendor_skus in sku_json.items():
            for sku in vendor_skus:
                self.add_sku_data(
                    vendor,
                    sku["region"],
                    sku["description"],
                    sku["sku_id"],
                    sku["unit_cost"],
                    sku["cost_basis"],
                )

    def add_sku_data(
        self, platform, region, description, sku_id, unit_cost, cost_basis
    ):
        self._sql_executor.execute_in_db(
            f"INSERT INTO SkuData(platform, region, description, sku_id, unit_cost, cost_basis) "
            f"values ('{platform}', '{region}', '{description}', "
            f"'{sku_id}', {unit_cost}, '{cost_basis}')"
        )

    def add_usage_estimate(self, description: str, estimate: float, category: str):
        self._sql_executor.execute_in_db(
            f"INSERT INTO SkuEstimates(description, estimate, category) "
            f"values ('{description}', {estimate}, '{category}')"
        )


class CostEstimator:
    """Estimates cost for a single cost model"""

    def __init__(
        self,
        sql_executor: SqlExecutor,
        sku_json: dict,
        cost_yaml,
        metrics: dict,
    ):
        self._sql_executor = sql_executor
        self._sku_json = sku_json
        self._cost_yaml = cost_yaml
        self._metrics = metrics

        self._estimator_tables = EstimatorTables(sql_executor)
        self._expression_evaluator = ExpressionEvaluator(sql_executor)

        self._region = read_from_yaml_or_die(
            self._cost_yaml, ["cost_model_metadata", "region"]
        )
        self._num_requests_metric = read_from_yaml_or_die(
            self._cost_yaml, ["cost_model_metadata", "num_requests_metric"]
        )

    def get_metrics(self):
        return self._metrics

    def run(self):
        """Run the cost estimator and generate estimates in the COstEstimates table."""

        if self._sku_json:
            self._estimator_tables.load_sku_data(self._sku_json)

        for metric, value in self._metrics.items():
            try:
                # only add values from metrics that can be converted to floats
                float(value)
                self._expression_evaluator.add_to_context(metric, value)
            except Exception:
                pass

        # Add all the defined values to the context.
        # The usages will be estimated will all these variables defined.
        for key, value in read_from_yaml_or_die(
            self._cost_yaml, ["defined_values"]
        ).items():
            self._expression_evaluator.add_to_context(key, value)

        usage_estimations = read_from_yaml_or_die(
            self._cost_yaml, ["usage_estimations"]
        )
        for category, value in usage_estimations.items():
            for sku, sku_usage_estimate in value.items():
                try:
                    estimate = self._expression_evaluator.get_value(
                        str(sku_usage_estimate)
                    )
                    self._estimator_tables.add_usage_estimate(
                        sku, estimate[0][0], category
                    )
                except Exception as e:
                    logger.error(
                        "Error in evaluating %s : %s . %s",
                        str(sku),
                        str(sku_usage_estimate),
                        str(e),
                    )

        num_requests = self._expression_evaluator.get_value(self._num_requests_metric)[
            0
        ][0]
        self._sql_executor.execute_in_db(
            f"""CREATE TABLE CostEstimates as
                SELECT se.category as category,
                    se.description as sku_description,
                    sd.sku_id as sku_id,
                    sd.unit_cost as unit_cost,
                    sd.cost_basis as cost_basis,
                    se.estimate as estimated_usage,
                    sd.unit_cost*se.estimate as estimated_cost,
                    se.estimate * 1e6 / {num_requests} as estimated_usage_per_million,
                    sd.unit_cost*se.estimate * 1e6 / {num_requests} as estimated_cost_per_million
                    FROM SkuEstimates se
                    LEFT OUTER JOIN SkuData sd
                        USING(description)
                    WHERE (sd.region = '{self._region}' OR sd.region is NULL)
                    """
        )
