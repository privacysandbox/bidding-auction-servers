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

"""Tool for estimating costs."""

import json
import yaml
import logging
from datetime import datetime

from utils import (
    read_from_yaml_or_die,
    write_to_named_or_temp_file,
    dict_to_csv,
    log_and_get_file_path,
)
from sql_expr import SqlExecutor
from metrics_loader import (
    CsvFileMetricsLoader,
    GcpMetricsLoader,
    AwsMetricsLoader,
)
from render import CsvRenderer
from estimator import CostEstimator

logger = logging.getLogger(__name__)


class CostTool:
    """Tool for estimating costs and rendering the outputs."""

    def get_cost_model_data(self, selected_model_name: str, cost_yaml):
        possible_models = []
        for data in cost_yaml:
            if data:
                possible_models.append(data["cost_model_metadata"]["name"])
            if data and data["cost_model_metadata"]["name"] == selected_model_name:
                return data
        raise Exception(
            f"Model named {selected_model_name} not found in the cost models file. "
            f"Available models: {possible_models}"
        )

    def load_metrics(self, args, cost_yaml, selected_model_name: str) -> dict:
        """Loads metrics and from the required environment, or from a file.
        The metric values are returned as a dict. Also adds a few sys config
        variables.
        """

        metrics = {}
        arg_params = {}
        arg_params["cost_model"] = selected_model_name

        # Add the key/value params that were given on the command line
        if args.param is not None:
            for param in args.param:
                arg_params[param[0].strip()] = param[1].strip()

        if args.metrics_file:
            metrics.update(
                CsvFileMetricsLoader(args.metrics_file).get_metrics(cost_yaml)
            )
            # add arg params, overwriting metrics from the file if there is conflict
            metrics.update(arg_params)
            return metrics

        vendor = read_from_yaml_or_die(cost_yaml, ["cost_model_metadata", "vendor"])

        if args.gcp_metrics_download:
            if vendor != "gcp":
                raise Exception(
                    f"gcp download requested, but vendor in cost model "
                    f"{selected_model_name} was {vendor}."
                )
            project = args.gcp_metrics_download[0]
            env = args.gcp_metrics_download[1]
            start_time = datetime.fromisoformat(args.gcp_metrics_download[2])
            end_time = datetime.fromisoformat(args.gcp_metrics_download[3])

            arg_params["gcp_project"] = project
            arg_params["gcp_environment"] = env
            arg_params["start_time"] = str(start_time)
            arg_params["end_time"] = str(end_time)
            arg_params["test.duration"] = int(
                (end_time - start_time).total_seconds() / 3600
            )
            metrics.update(
                GcpMetricsLoader(project, env, start_time, end_time).get_metrics(
                    cost_yaml
                )
            )

        if args.aws_metrics_download:
            if vendor != "aws":
                raise Exception(
                    f"aws download requested, but vendor in cost model "
                    f"{selected_model_name} was {vendor}."
                )
            env = args.aws_metrics_download[0]
            region = read_from_yaml_or_die(cost_yaml, ["cost_model_metadata", "region"])
            start_time = datetime.fromisoformat(args.aws_metrics_download[1])
            end_time = datetime.fromisoformat(args.aws_metrics_download[2])

            arg_params["aws_region"] = region
            arg_params["aws_environment"] = env
            arg_params["start_time"] = str(start_time)
            arg_params["end_time"] = str(end_time)
            arg_params["test.duration"] = int(
                (end_time - start_time).total_seconds() / 3600
            )

            metrics.update(
                AwsMetricsLoader(env, region, start_time, end_time).get_metrics(
                    cost_yaml
                )
            )

        # add arg params, overwriting metrics from the file if there is conflict
        metrics.update(arg_params)

        write_to_named_or_temp_file(
            args.save_downloaded_metrics, dict_to_csv(metrics), "Metrics"
        )
        return metrics

    def run_internal(self, cost_yaml, metrics, sku_json):
        sql_executor = SqlExecutor()
        estimator = CostEstimator(sql_executor, sku_json, cost_yaml, metrics)
        estimator.run()

        return CsvRenderer(sql_executor, cost_yaml, metrics).render()

    def run(
        self,
        args,
    ) -> str:
        """Run the estimations and return the rendered output."""
        selected_model_name = args.cost_model
        cost_file_path = log_and_get_file_path(
            args.cost_model_file, "Opening", "Cost model"
        )
        with open(cost_file_path) as cost_model_file:
            full_yaml = list(yaml.safe_load_all(cost_model_file))

        sku_json = {}
        if args.sku_file:
            sku_file_path = log_and_get_file_path(args.sku_file, "Opening", "SKU")
            with open(sku_file_path) as skus:
                sku_json = json.load(skus)

        selected_model_yaml = self.get_cost_model_data(selected_model_name, full_yaml)
        metrics = self.load_metrics(args, selected_model_yaml, selected_model_name)
        return self.run_internal(selected_model_yaml, metrics, sku_json)
