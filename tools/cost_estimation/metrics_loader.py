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
"""Module for loading metrics."""

import csv
import re
import logging
import datetime
import boto3
from google.cloud import monitoring_v3
from google.api import metric_pb2
from datetime import timezone
from utils import log_and_get_file_path

logger = logging.getLogger(__name__)


def create_requested_metrics_list(metrics) -> list:
    """Creates a list of dicts, each corresponding to 1 metric that needs to be
    downloaded."""
    ret = []
    for item in metrics:
        metric = item["metric"]
        aggregation = item.get("aggregation", None)
        for service in item["service"]:
            label = item.get("label", None)
            copy_to_variable = item.get("copy_to_variable", None)
            metric_string = service + ":" + metric
            if label:
                metric_string += ":" + label.replace(" ", "_")
            res = {
                "service": service,
                "metric": metric,
                "aggregation": aggregation,
                "label": label,
                "copy_to_variable": copy_to_variable,
                "metric_string": metric_string,
            }
            ret.append(res)
    return ret


class MetricsLoader:
    """Abstract Class for loading metrics."""

    def get_metrics(self, cost_yaml) -> dict:
        pass


class CsvFileMetricsLoader(MetricsLoader):
    """Class for loading metrics from a file."""

    def __init__(self, csv_filename: str):
        self._metrics = {}
        csv_file_path = log_and_get_file_path(csv_filename, "Opening", "Metrics")
        with open(csv_file_path) as metrics_csv_file:
            metrics = csv.reader(metrics_csv_file)
            for row in metrics:
                if row and not re.match(r"^\s*#", row[0]):
                    self._metrics[row[0].strip()] = row[1].strip()

    def get_metrics(self, cost_yaml) -> dict:
        return self._metrics


class GcpDownloader:
    """Downloader for GCP metrics."""

    def __init__(self, project: str, environment: str, start_time, end_time):
        self._project = project
        self._environment = environment
        self._start_time = start_time
        self._end_time = end_time
        self._client = monitoring_v3.MetricServiceClient()

    def create_gcp_monitoring_payload(self, service: str, metric: str, label: str):
        """Creates a payload to call the list_time_series_api with."""
        interval = monitoring_v3.TimeInterval(
            {"end_time": self._end_time, "start_time": self._start_time}
        )
        aggregate = monitoring_v3.Aggregation(
            per_series_aligner="ALIGN_NONE",
            cross_series_reducer="REDUCE_NONE",
            group_by_fields=["metric.labels.service_name"],
            alignment_period="3000s",
        )
        filter_string = (
            f'metric.type="workload.googleapis.com/{metric}" '
            f'AND metric.label.deployment_environment="{self._environment}" '
            f'AND metric.labels.service_name="{service}"'
        )
        if label:
            filter_string += f' AND metric.label.label="{label}"'
        return {
            "name": f"projects/{self._project}",
            "filter": filter_string,
            "interval": interval,
            "view": monitoring_v3.ListTimeSeriesRequest.TimeSeriesView.FULL,
            "aggregation": aggregate,
        }

    def download_metric(self, metric):
        """Downloads a single metric from GCP by calling the list_time_series monitoring API."""
        payload = self.create_gcp_monitoring_payload(
            service=metric["service"], metric=metric["metric"], label=metric["label"]
        )
        metric_string = metric["metric_string"]
        try:
            result = self._client.list_time_series(payload)
            return result
        except Exception as e:
            logger.error("Error retrieving %s %s", metric_string, str(e))

        return None


class GcpAggregator:
    """Aggregator to aggregate metrics downloaded from Gcp."""

    def get_value(self, point, value_type):
        """Gets the value from the point, given the value_type."""
        if value_type == metric_pb2.MetricDescriptor.ValueType.INT64:
            return point.value.int64_value
        if value_type == metric_pb2.MetricDescriptor.ValueType.DOUBLE:
            return point.value.double_value
        if value_type == metric_pb2.MetricDescriptor.ValueType.DISTRIBUTION:
            return (
                point.value.distribution_value.mean
                * point.value.distribution_value.count
            )
        raise Exception(f"Unsupported value_type f{value_type}")

    def find_inc(self, ppoint, point, value_type):
        """Fid the increment between ppoint and point."""
        if (
            hasattr(ppoint, "interval")
            and hasattr(point, "interval")
            and ppoint.interval.start_time != point.interval.start_time
        ):
            return 0

        if value_type == metric_pb2.MetricDescriptor.ValueType.INT64:
            return ppoint.value.int64_value - point.value.int64_value
        if value_type == metric_pb2.MetricDescriptor.ValueType.DOUBLE:
            return ppoint.value.double_value - point.value.double_value
        if value_type == metric_pb2.MetricDescriptor.ValueType.DISTRIBUTION:
            return ppoint.value.distribution_value.mean * (
                ppoint.value.distribution_value.count
                - point.value.distribution_value.count
            )
        raise Exception(f"Unsupported value_type {value_type}")

    def find_cumulative_inc_value(self, points, value_type):
        ppoint = None
        total = 0
        for point in points:
            if ppoint:
                total += self.find_inc(ppoint, point, value_type)
            ppoint = point
        return total

    def find_total(self, points, value_type):
        total = 0
        for point in points:
            total += self.get_value(point, value_type)
        return total

    def aggregate(self, metric_query_result):
        """Aggregates the query result."""
        metric_kind = None
        value_type = None
        total_value = 0
        total_denominator = 0
        for result_page in metric_query_result:
            if metric_kind:
                assert metric_kind == result_page.metric_kind
            else:
                metric_kind = result_page.metric_kind

            if value_type:
                assert value_type == result_page.value_type
            else:
                value_type = result_page.value_type

            if metric_kind == metric_pb2.MetricDescriptor.MetricKind.CUMULATIVE:
                total_value += self.find_cumulative_inc_value(
                    result_page.points, value_type
                )
                total_denominator = 1
            elif metric_kind == metric_pb2.MetricDescriptor.MetricKind.GAUGE:
                total_value += self.find_total(result_page.points, value_type)
                total_denominator += len(result_page.points)
            else:
                raise Exception(f"Unsupported metric_kind {metric_kind}")
        if total_denominator == 0:
            total_denominator = 1

        return total_value / total_denominator


class GcpMetricsLoader(MetricsLoader):
    """Loads metrics from Gcp."""

    def __init__(self, project, environment, start_time, end_time):
        self._downloader = GcpDownloader(
            project=project,
            environment=environment,
            start_time=start_time,
            end_time=end_time,
        )
        self._aggregator = GcpAggregator()

    def get_metrics(self, cost_yaml):
        metrics = cost_yaml["download_metrics"]
        ret = {}
        for metric in create_requested_metrics_list(metrics):
            result = self._downloader.download_metric(metric)
            ret[metric["metric_string"]] = self._aggregator.aggregate(result)

            # Check if we need to copy the value to a new variable
            copy_to_variable = metric.get("copy_to_variable", None)
            if copy_to_variable:
                ret[metric["service"] + ":" + copy_to_variable] = ret[
                    metric["metric_string"]
                ]
        return ret


class AwsDownloader:
    """Downloader for Aws metrics."""

    def __init__(self, environment: str, region: str, start_time, end_time):
        self._environment = environment
        self._region = region
        self._start_time = start_time
        self._end_time = end_time
        self._period = self.calculate_period(start_time, end_time)
        self._cloudwatch = boto3.client("cloudwatch", region_name=region)

    def seconds_to_now(self, other: datetime):
        now_time = datetime.datetime.now().replace(tzinfo=timezone.utc)
        other_time = other.replace(tzinfo=timezone.utc)
        return (now_time - other_time).total_seconds()

    def calculate_period(self, start_time: datetime, end_time: datetime):
        """
        Calculate which granularity points to request. For reasons selecting the costants, see
        https://boto3.amazonaws.com/v1/documentation/api/latest/reference/services/cloudwatch/client/get_metric_data.html
        """
        seconds = (end_time - start_time).total_seconds()
        seconds_from_start_to_now = self.seconds_to_now(start_time)
        if seconds <= 60 * 60 * 6 and seconds_from_start_to_now <= 60 * 60 * 24 * 14:
            return 60  # Do per minute calculation for <= 6hr and less than 14 days ago
        if (
            seconds <= 60 * 60 * 24 * 7
            and seconds_from_start_to_now <= 60 * 60 * 24 * 62
        ):
            return 300  # Do per 5-minute calculation for <= 1Week and <= 62 days ago
        return 3600  # Otherwise do daily calculations

    def download_metric(self, service: str, metric: str, aggregation: str, label: str):
        """ "Downloads a single metric from Aws."""

        dimensions = [{"Name": "OTelLib", "Value": service}]
        if label:
            dimensions.append({"Name": "label", "Value": label})
        else:
            dimensions.append(
                {"Name": "deployment.environment", "Value": self._environment}
            )

        return self._cloudwatch.get_metric_statistics(
            Namespace=service,
            MetricName=metric,
            StartTime=self._start_time,
            EndTime=self._end_time,
            Period=self._period,
            Statistics=[aggregation],
            Dimensions=dimensions,
        )


class AwsAggregator:
    """Aggregator to aggregate metrics downloaded from Aws."""

    def aggregate(self, request, result):
        """Aggregates the result from the downloader."""
        total = 0.0
        aggregation = request["aggregation"]
        for point in result["Datapoints"]:
            total += point[aggregation]

        if aggregation == "Sum":
            return total
        if aggregation == "Average":
            return total / max(len(result["Datapoints"]), 1)
        raise Exception(f"Unsupported aggregation {aggregation}")


class AwsMetricsLoader(MetricsLoader):
    """A metrics loader for loading Aws metrics."""

    def __init__(self, environment, region, start_time, end_time):
        self._downloader = AwsDownloader(environment, region, start_time, end_time)
        self._aggregator = AwsAggregator()

    def get_metrics(self, cost_yaml):
        metrics = cost_yaml["download_metrics"]
        ret = {}
        for request in create_requested_metrics_list(metrics):
            result = self._downloader.download_metric(
                service=request["service"],
                metric=request["metric"],
                aggregation=request["aggregation"],
                label=request.get("label", None),
            )
            ret[request["metric_string"]] = self._aggregator.aggregate(request, result)

            # Check if we need to copy the value to a new variable
            copy_to_variable = request.get("copy_to_variable", None)
            if copy_to_variable:
                ret[request["service"] + ":" + copy_to_variable] = ret[
                    request["metric_string"]
                ]
        return ret
