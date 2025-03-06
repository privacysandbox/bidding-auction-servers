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

"""Main entry point for the cost tool"""

import argparse
import logging
from utils import write_to_named_or_temp_file
from cost_tool import CostTool


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--loglevel",
        default="warning",
        choices=["info", "debug", "warning", "error", "fatal"],
        help="Provide logging level. Example --loglevel debug, default=warning",
    )

    # Estimate
    parser.add_argument(
        "--cost_model_file",
        type=str,
        default="cost.yaml",
        metavar="COST_MODEL_FILENAME",
        help="Provide the cost model yaml file. Default: cost.yaml",
    )

    parser.add_argument(
        "--cost_model",
        type=str,
        metavar="COST_MODEL_NAME",
        help="Select the cost model to use",
        required=True,
    )
    parser.add_argument(
        "--sku_file",
        default="sku.json",
        metavar="SKU_FILENAME",
        help="JSON-formatted file providing SKU pricing. " "Default: sku.json",
        required=False,
    )

    parser.add_argument(
        "--output_file",
        metavar="OUTPUT_FILENAME",
        help="Saves output to a file instead of the default, which prints to STDOUT",
        required=False,
    )

    # Metrics
    parser.add_argument(
        "--metrics_file",
        metavar="METRICS_FILENAME",
        help="Csv-formatted file providing metrics for use in the cost model",
    )

    parser.add_argument(
        "--aws_metrics_download",
        nargs=3,
        metavar=("ENVIRONMENT", "START_TIME", "END_TIME"),
        help="Download metrics from AWS. Args are <environment> <start time> <end time>. "
        "Only works if the selected cost model is an AWS model.",
    )

    parser.add_argument(
        "--gcp_metrics_download",
        nargs=4,
        metavar=("PROJECT", "ENVIRONMENT", "START_TIME", "END_TIME"),
        help="Download metrics from GCP. Args are <project> <environment> <start time> <end time>"
        "Only wors if the selected cost model is a GCP model.",
    )

    parser.add_argument(
        "--save_downloaded_metrics",
        metavar="METRICS_FILENAME",
        help="Save the downloaded metrics in a Csv file. This file can be used with the "
        "--metrics_file option in the future.",
    )

    # System config
    parser.add_argument(
        "--param",
        nargs=2,
        metavar=["KEY", "VALUE"],
        action="append",
        help="Arbitrary key-value parameters to pass as variables to the cost model. "
        "This argument can be given multiple times to pass multiple key-values "
        "into the cost model.",
    )

    args = parser.parse_args()
    logging.basicConfig(level=args.loglevel.upper())
    cost_tool = CostTool()

    output = cost_tool.run(args)
    if not args.output_file:
        print(output)  # only write to stdout if no file was given
    write_to_named_or_temp_file(args.output_file, output, "generated estimates")


if __name__ == "__main__":
    main()
