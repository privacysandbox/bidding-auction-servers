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
"""Renderers for cost estimates."""

import io
import csv
from string import Template
from sql_expr import SqlExecutor
from utils import dict_to_csv

ADDITIONAL_INFO_MESSAGE = (
    "The accuracy of the following estimates may vary. "
    "Please see https://github.com/privacysandbox/protected-auction-services-docs/"
    "blob/main/bidding_auction_cost_estimation_tool.md"
    "#factors-that-affect-accuracy-of-cost-estimates"
    " for interepreting these results."
)


def to_csv_string(*args):
    si = io.StringIO()
    cw = csv.writer(si, lineterminator="\n")
    for arg in args:
        cw.writerows(arg)
    return si.getvalue()


class MyTemplate(Template):
    """Template that uses identifiers allowing dashes"""

    idpattern = r"(?-i:[_a-zA-Z-][_a-zA-Z0-9-]*)"


class CsvRenderer:
    """A renderer that queries the CostEstimates tables and renders as CSV using a template."""

    def __init__(self, sql_executor: SqlExecutor, cost_yaml, metrics: dict):
        self._sql_executor = sql_executor
        self._cost_yaml = cost_yaml
        self._metrics = metrics

    def render(self) -> str:
        """Produces a rendered output from the CostEstimates tables.
        Returns the output as a string."""
        to_render = {}
        all_categories_template = ""
        for cat in self._sql_executor.execute_in_db(
            "SELECT DISTINCT category from SkuEstimates ORDER BY category"
        ):
            category = cat[0].strip()
            all_categories_template += f"${category}\n"
            results = self._sql_executor.execute_in_db(
                f"""
                SELECT sku_description,
                    sku_id,
                    printf("$%.2f",unit_cost),
                    cost_basis,
                    printf("%.2f",estimated_usage),
                    printf("$%.2f",estimated_cost),
                    printf("%.2f",estimated_usage_per_million),
                    printf("$%.2f",estimated_cost_per_million)
                FROM CostEstimates
                WHERE category = '{category}'
                ORDER BY sku_description"""
            )

            total = self._sql_executor.execute_in_db(
                f"""
                SELECT "Total",
                    "",
                    "",
                    "",
                    "",
                    printf("$%.2f",SUM(estimated_cost)),
                    "",
                    printf("$%.2f",SUM(estimated_cost_per_million))
                FROM CostEstimates
                WHERE category = '{category}'
                GROUP BY category"""
            )

            to_render[category] = to_csv_string(
                [[category]],
                [
                    [
                        "Sku name",
                        "Sku-Id",
                        "Unit Cost",
                        "Cost Basis",
                        "Estimated Units",
                        "Estimated Cost",
                        "Estimated Units Per Million Queries",
                        "Estimated Cost Per Million Queries",
                    ]
                ],
                results,
                total,
            )
        summary_results = self._sql_executor.execute_in_db(
            """SELECT category, printf("$%.2f", SUM(estimated_cost_per_million))
                    FROM CostEstimates
                    GROUP BY category
                    ORDER BY category"""
        )

        summary_total = self._sql_executor.execute_in_db(
            """SELECT "Total", printf("$%.2f", SUM(estimated_cost_per_million))
                    FROM CostEstimates"""
        )

        all_categories_template = "$Summary\n" + all_categories_template + "\n$Metrics"
        to_render["Summary"] = to_csv_string(
            [[ADDITIONAL_INFO_MESSAGE]],
            [[]],
            [["Summary"]],
            [["Category", "Estimated Cost Per Million Queries"]],
            summary_results,
            summary_total,
        )
        to_render["Metrics"] = "Metric, Value\n" + dict_to_csv(self._metrics)
        template_str = self._cost_yaml.get("results_template", all_categories_template)
        template = MyTemplate(template_str)
        return template.substitute(to_render)
