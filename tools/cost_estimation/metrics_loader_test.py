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
"""Tests for metrics_loader."""

from collections import namedtuple
from datetime import datetime
import unittest
from unittest.mock import MagicMock, mock_open, patch

from google.api import metric_pb2
from metrics_loader import (
    CsvFileMetricsLoader,
    GcpDownloader,
    GcpAggregator,
    GcpMetricsLoader,
    AwsDownloader,
    AwsAggregator,
    AwsMetricsLoader,
)


class TestCsvFileMetricsLoader(unittest.TestCase):
    """Tests the CsvFileMetricsLoader class."""

    def test_csv_file_metrics_loader_basic(self):
        csv_data = "metric1,value1\nmetric2,value2\n"
        m = mock_open(read_data=csv_data)
        with patch("builtins.open", m):
            loader = CsvFileMetricsLoader("test_metrics.csv")
            expected_metrics = {"metric1": "value1", "metric2": "value2"}
            self.assertEqual(loader.get_metrics([]), expected_metrics)

    def test_csv_file_metrics_loader_comments(self):
        csv_data = "metric1,value1\n  # metric2,value2\n  metric3  , value3"
        m = mock_open(read_data=csv_data)
        with patch("builtins.open", m):
            loader = CsvFileMetricsLoader("test_metrics.csv")
            expected_metrics = {"metric1": "value1", "metric3": "value3"}
            self.assertEqual(loader.get_metrics([]), expected_metrics)

    def test_csv_file_metrics_loader_whitespace(self):
        csv_data = " metric1 ,  value1\n  # metric2,value2\n  metric3  , value3"
        m = mock_open(read_data=csv_data)
        with patch("builtins.open", m):
            loader = CsvFileMetricsLoader("test_metrics.csv")
            expected_metrics = {"metric1": "value1", "metric3": "value3"}
            self.assertEqual(loader.get_metrics([]), expected_metrics)


class TestGcpDownloader(unittest.TestCase):
    """Tests the GcpDownloader."""

    def setUp(self):
        """Set up for test methods."""
        self.project = "test-project"
        self.environment = "test-env"
        self.start_time = datetime(2024, 1, 1)
        self.end_time = datetime(2024, 1, 2)
        self.downloader = GcpDownloader(
            self.project, self.environment, self.start_time, self.end_time
        )

        self.downloader._client = MagicMock()  # pylint: disable=W0212
        # pylint: disable=W0212
        self.downloader._client.list_time_series.return_value = {
            "some_metric": "some_value"
        }

    def test_create_gcp_monitoring_payload(self):
        """Tests create_gcp_monitoring_payload."""
        service = "test-service"
        metric = "test-metric"
        label = "test-label"

        payload = self.downloader.create_gcp_monitoring_payload(service, metric, label)

        # Assertions to check if the payload is correctly formed
        self.assertEqual(payload["name"], f"projects/{self.project}")
        self.assertIn(
            f'metric.type="workload.googleapis.com/{metric}"', payload["filter"]
        )
        self.assertIn(
            f'metric.label.deployment_environment="{self.environment}"',
            payload["filter"],
        )
        self.assertIn(f'metric.labels.service_name="{service}"', payload["filter"])
        self.assertIn(
            f'metric.label.label="{label}"', payload["filter"]
        )  # Check if label is included


class TestGcpMetricsLoader(unittest.TestCase):
    """Tests for the GcpMetricsLoader."""

    @patch("google.cloud.monitoring_v3.MetricServiceClient")
    def test_get_metrics(self, mock_metrics_client):
        """Tests get_metrics method for the GcpMetricsLoader."""
        cost_yaml = {
            "cost_model_metadata": {
                "region": "us-west-1",
                "num_requests_metric": "sfe:request.count",
                "name": "basic_model",
            },
            "download_metrics": [
                {
                    "metric": "test_metric_1",
                    "service": ["service_a", "service_b"],
                    "label": "test_label_1",
                },
                {
                    "metric": "test_metric_2",
                    "aggregation": "Average",
                    "service": ["service_c"],
                },
            ],
        }

        # Mock the response from list_time_series
        def side_effect(*args):
            metric_result = MagicMock()
            Value = namedtuple("Value", "value")
            OneOf = namedtuple(
                "OneOf", ["distribution_value", "int64_value", "double_value"]
            )
            Distribution = namedtuple("Distribution", ["count", "mean"])
            if (
                "test_metric_1" in args[0]["filter"]
                and "service_a" in args[0]["filter"]
            ):
                metric_result.metric_kind = (
                    metric_pb2.MetricDescriptor.MetricKind.CUMULATIVE
                )
                metric_result.value_type = (
                    metric_pb2.MetricDescriptor.ValueType.DISTRIBUTION
                )
                metric_result.points = [
                    Value(OneOf(Distribution(5, 50), None, None)),  # 50 * 2 = 100
                    Value(OneOf(Distribution(3, 30), None, None)),  # 30 * 2 = 60
                    Value(OneOf(Distribution(1, 10), None, None)),  # Total  = 160
                ]

            if (
                "test_metric_1" in args[0]["filter"]
                and "service_b" in args[0]["filter"]
            ):
                metric_result.metric_kind = (
                    metric_pb2.MetricDescriptor.MetricKind.CUMULATIVE
                )
                metric_result.value_type = metric_pb2.MetricDescriptor.ValueType.INT64
                metric_result.points = [
                    Value(OneOf(None, 5, None)),  # 5 - 3 = 2
                    Value(OneOf(None, 3, None)),  # 3 - 2 = 1
                    Value(OneOf(None, 2, None)),  # Total = 3
                ]

            if "test_metric_2" in args[0]["filter"]:
                metric_result.metric_kind = metric_pb2.MetricDescriptor.MetricKind.GAUGE
                metric_result.value_type = metric_pb2.MetricDescriptor.ValueType.DOUBLE
                metric_result.points = [
                    Value(OneOf(None, None, 1)),  #
                    Value(OneOf(None, None, 2)),  #
                    Value(OneOf(None, None, 3)),  # Average = 6/3 = 2
                ]
            return [metric_result]

        retval_mock = MagicMock()
        retval_mock.list_time_series = MagicMock(side_effect=side_effect)
        mock_metrics_client.return_value = retval_mock
        expected = {
            "service_a:test_metric_1:test_label_1": 160.0,  # 100 + 60 above
            "service_b:test_metric_1:test_label_1": 3.0,  # 2 + 1 above
            "service_c:test_metric_2": 2.0,
        }  # (1 + 2 + 3) /3 above

        loader = GcpMetricsLoader(
            "project_id",
            "env_id",
            datetime.fromisoformat("2024-12-12"),
            datetime.fromisoformat("2024-12-13"),
        )
        self.assertEqual(expected, loader.get_metrics(cost_yaml))


class TestGcpAggregator(unittest.TestCase):
    """Tests for GcpAggregator."""

    def setUp(self):
        """Set up for test methods."""
        self.aggregator = GcpAggregator()

    def test_get_value(self):
        """Test getting values for different value types."""
        # Mock a point with int64_value
        point_int64 = MagicMock()
        point_int64.value.int64_value = 10
        self.assertEqual(
            self.aggregator.get_value(
                point_int64, metric_pb2.MetricDescriptor.ValueType.INT64
            ),
            10,
        )

        # Mock a point with double_value
        point_double = MagicMock()
        point_double.value.double_value = 3.14
        self.assertEqual(
            self.aggregator.get_value(
                point_double, metric_pb2.MetricDescriptor.ValueType.DOUBLE
            ),
            3.14,
        )

        # Mock a point with distribution_value
        point_dist = MagicMock()
        point_dist.value.distribution_value.mean = 2.5
        point_dist.value.distribution_value.count = 5
        self.assertEqual(
            self.aggregator.get_value(
                point_dist, metric_pb2.MetricDescriptor.ValueType.DISTRIBUTION
            ),
            12.5,
        )

        # Test for unsupported value type
        with self.assertRaises(Exception) as context:
            self.aggregator.get_value(point_int64, "INVALID_TYPE")
        self.assertIn("Unsupported value_type", str(context.exception))

    def test_find_inc(self):
        """Test finding increments for different value types."""
        # Mock points with int64_value
        ppoint_int64 = MagicMock()
        ppoint_int64.value.int64_value = 20
        ppoint_int64.interval.start_time = 0

        point_int64 = MagicMock()
        point_int64.value.int64_value = 10
        point_int64.interval.start_time = 0

        self.assertEqual(
            self.aggregator.find_inc(
                ppoint_int64, point_int64, metric_pb2.MetricDescriptor.ValueType.INT64
            ),
            10,
        )

        # Mock points with double_value
        ppoint_double = MagicMock()
        ppoint_double.value.double_value = 5.5
        ppoint_double.interval.start_time = 0

        point_double = MagicMock()
        point_double.value.double_value = 2.5
        point_double.interval.start_time = 0
        self.assertEqual(
            self.aggregator.find_inc(
                ppoint_double,
                point_double,
                metric_pb2.MetricDescriptor.ValueType.DOUBLE,
            ),
            3.0,
        )

        # Mock points with distribution_value
        ppoint_dist = MagicMock()
        ppoint_dist.value.distribution_value.mean = 3.0
        ppoint_dist.value.distribution_value.count = 10
        ppoint_dist.interval.start_time = 0

        point_dist = MagicMock()
        point_dist.value.distribution_value.count = 5
        point_dist.interval.start_time = 0

        self.assertEqual(
            self.aggregator.find_inc(
                ppoint_dist,
                point_dist,
                metric_pb2.MetricDescriptor.ValueType.DISTRIBUTION,
            ),
            15.0,  # 3.0 * (10 - 5)
        )

        # Test for unsupported value type
        with self.assertRaises(Exception) as context:
            self.aggregator.find_inc(ppoint_int64, point_int64, "INVALID_TYPE")
        self.assertIn("Unsupported value_type", str(context.exception))

    def test_find_inc_changed_start(self):
        """Test finding increments when start time changes.
        See https://cloud.google.com/monitoring/api
        /ref_v3/rest/v3/projects.metricDescriptors#MetricKind.ENUM_VALUES.CUMULATIVE
        for details.
        """
        # Mock points with int64_value
        ppoint_int64 = MagicMock()
        ppoint_int64.value.int64_value = 20
        ppoint_int64.interval.start_time = 0

        point_int64 = MagicMock()
        point_int64.value.int64_value = 10
        point_int64.interval.start_time = 9999
        self.assertEqual(
            self.aggregator.find_inc(
                ppoint_int64, point_int64, metric_pb2.MetricDescriptor.ValueType.INT64
            ),
            0,
        )

        # Mock points with double_value
        ppoint_double = MagicMock()
        ppoint_double.value.double_value = 5.5
        ppoint_double.interval.start_time = 0
        point_double = MagicMock()
        point_double.value.double_value = 2.5
        point_double.interval.start_time = 9999
        self.assertEqual(
            self.aggregator.find_inc(
                ppoint_double,
                point_double,
                metric_pb2.MetricDescriptor.ValueType.DOUBLE,
            ),
            0,
        )

        # Mock points with distribution_value
        ppoint_dist = MagicMock()
        ppoint_dist.value.distribution_value.mean = 3.0
        ppoint_dist.value.distribution_value.count = 10
        ppoint_dist.interval.start_time = 0
        point_dist = MagicMock()
        point_dist.value.distribution_value.count = 5
        point_dist.interval.start_time = 9999
        self.assertEqual(
            self.aggregator.find_inc(
                ppoint_dist,
                point_dist,
                metric_pb2.MetricDescriptor.ValueType.DISTRIBUTION,
            ),
            0,
        )

        # Test for unsupported value type
        point_int64.interval.start_time = 0
        with self.assertRaises(Exception) as context:
            self.aggregator.find_inc(ppoint_int64, point_int64, "INVALID_TYPE")
        self.assertIn("Unsupported value_type", str(context.exception))

    def test_find_cumulative_inc_value(self):
        """Test calculating cumulative increment."""
        # Mock points with int64_value
        points_int64 = [
            MagicMock(
                value=MagicMock(int64_value=30), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(int64_value=20), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(int64_value=10), interval=MagicMock(start_time=0)
            ),
        ]
        self.assertEqual(
            self.aggregator.find_cumulative_inc_value(
                points_int64, metric_pb2.MetricDescriptor.ValueType.INT64
            ),
            20,  # (30-20) + (20-10)
        )

        # Mock points with double_value
        points_double = [
            MagicMock(
                value=MagicMock(double_value=5.5), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(double_value=3.5), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(double_value=1.5), interval=MagicMock(start_time=0)
            ),
        ]
        self.assertEqual(
            self.aggregator.find_cumulative_inc_value(
                points_double, metric_pb2.MetricDescriptor.ValueType.DOUBLE
            ),
            4.0,  # (5.5-3.5) + (3.5-1.5)
        )

    def test_find_total(self):
        """Test calculating total value."""
        # Mock points with int64_value
        points_int64 = [
            MagicMock(
                value=MagicMock(int64_value=10), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(int64_value=20), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(int64_value=30), interval=MagicMock(start_time=0)
            ),
        ]
        self.assertEqual(
            self.aggregator.find_total(
                points_int64, metric_pb2.MetricDescriptor.ValueType.INT64
            ),
            60,
        )

        # Mock points with double_value
        points_double = [
            MagicMock(
                value=MagicMock(double_value=1.5), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(double_value=3.5), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(double_value=5.5), interval=MagicMock(start_time=0)
            ),
        ]
        self.assertEqual(
            self.aggregator.find_total(
                points_double, metric_pb2.MetricDescriptor.ValueType.DOUBLE
            ),
            10.5,
        )

    def test_aggregate_cumulative(self):
        """Test aggregating cumulative metrics."""
        # Mock result page with cumulative metric
        result_page = MagicMock()
        result_page.metric_kind = metric_pb2.MetricDescriptor.MetricKind.CUMULATIVE
        result_page.value_type = metric_pb2.MetricDescriptor.ValueType.INT64
        result_page.points = [
            MagicMock(
                value=MagicMock(int64_value=30), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(int64_value=20), interval=MagicMock(start_time=0)
            ),
            MagicMock(
                value=MagicMock(int64_value=10), interval=MagicMock(start_time=0)
            ),
        ]
        metric_query_result = [result_page]
        self.assertEqual(
            self.aggregator.aggregate(metric_query_result), 20
        )  # (30-20) + (20-10)

    def test_aggregate_gauge(self):
        """Test aggregating gauge metrics."""
        # Mock result page with gauge metric
        result_page = MagicMock()
        result_page.metric_kind = metric_pb2.MetricDescriptor.MetricKind.GAUGE
        result_page.value_type = metric_pb2.MetricDescriptor.ValueType.DOUBLE
        result_page.points = [
            MagicMock(value=MagicMock(double_value=5.5)),
            MagicMock(value=MagicMock(double_value=3.5)),
            MagicMock(value=MagicMock(double_value=1.5)),
        ]
        metric_query_result = [result_page]
        self.assertEqual(
            self.aggregator.aggregate(metric_query_result), 3.5
        )  # 10.5 / 3

    def test_aggregate_unsupported_metric_kind(self):
        """Test for unsupported metric kind."""
        # Mock result page with unsupported metric kind
        result_page = MagicMock()
        result_page.metric_kind = "UNSUPPORTED_METRIC_KIND"
        metric_query_result = [result_page]
        with self.assertRaises(Exception) as context:
            self.aggregator.aggregate(metric_query_result)
        self.assertIn("Unsupported metric_kind", str(context.exception))


class TestAwsDownloader(unittest.TestCase):
    """Tests for the AwsDownloader."""

    def setUp(self):
        self.environment = "test_env"
        self.start_time = datetime(2024, 1, 1)
        self.end_time = datetime(2024, 1, 2)
        self.expected_period = 300
        self._region = "us-west-1"
        self.downloader = AwsDownloader(
            self.environment, self._region, self.start_time, self.end_time
        )
        self._mock_cloudwatch = MagicMock()
        self.downloader._cloudwatch = self._mock_cloudwatch  # pylint: disable=W0212
        # pylint: disable=W0212
        self._mock_cloudwatch.get_metric_statistics.return_value = {
            "some_metric": "some_value"
        }
        self.dtpatcher = patch("datetime.datetime")
        mock_dt = self.dtpatcher.start()
        mock_dt.now.return_value = datetime(2024, 1, 6, 10, 0, 0)

    def test_calculate_period_less_than_6_hours(self):
        start_time = datetime(2024, 1, 1, 10, 0, 0)
        end_time = datetime(2024, 1, 1, 15, 0, 0)  # 5 hours
        self.assertEqual(self.downloader.calculate_period(start_time, end_time), 60)

    def test_calculate_period_less_than_1_week(self):
        start_time = datetime(2024, 1, 1, 10, 0, 0)
        end_time = datetime(2024, 1, 5, 10, 0, 0)  # 4 days
        self.assertEqual(self.downloader.calculate_period(start_time, end_time), 300)

    def test_calculate_period_more_than_1_week(self):
        start_time = datetime(2024, 1, 1, 10, 0, 0)
        end_time = datetime(2024, 1, 15, 10, 0, 0)  # 14 days
        self.assertEqual(self.downloader.calculate_period(start_time, end_time), 3600)

    def test_download_metric_with_label(self):
        service = "test_service"
        metric = "test_metric"
        aggregation = "Sum"
        label = "test_label"

        self.downloader.download_metric(service, metric, aggregation, label)

        self._mock_cloudwatch.get_metric_statistics.assert_called_with(
            Namespace=service,
            MetricName=metric,
            StartTime=self.start_time,
            EndTime=self.end_time,
            Period=self.expected_period,
            Statistics=[aggregation],
            Dimensions=[
                {"Name": "OTelLib", "Value": service},
                {"Name": "label", "Value": label},
            ],
        )

    def test_download_metric_without_label(self):
        service = "test_service"
        metric = "test_metric"
        aggregation = "Average"
        label = None  # No label provided

        self.downloader.download_metric(service, metric, aggregation, label)

        self._mock_cloudwatch.get_metric_statistics.assert_called_with(
            Namespace=service,
            MetricName=metric,
            StartTime=self.start_time,
            EndTime=self.end_time,
            Period=self.expected_period,
            Statistics=[aggregation],
            Dimensions=[
                {"Name": "OTelLib", "Value": service},
                {"Name": "deployment.environment", "Value": self.environment},
            ],
        )


class TestAwsMetricsLoader(unittest.TestCase):
    """Tests for the AwsMetricsLoader."""

    @patch("boto3.client")
    def test_get_metrics(self, mock_boto3):
        """Tests get_metrics for the AwsMetricsLoader."""
        cost_yaml = {
            "cost_model_metadata": {
                "region": "us-west-1",
                "num_requests_metric": "sfe:request.count",
                "name": "basic_model",
            },
            "download_metrics": [
                {
                    "metric": "test_metric_1",
                    "aggregation": "Sum",
                    "service": ["service_a", "service_b"],
                    "label": "test_label_1",
                },
                {
                    "metric": "test_metric_2",
                    "aggregation": "Average",
                    "service": ["service_c"],
                },
            ],
        }

        mock_cloudwatch = MagicMock()

        # Mock the response from get_metric_statistics
        def side_effect(**kw_args):
            return {
                "Datapoints": [
                    {kw_args["Statistics"][0]: 10},
                    {kw_args["Statistics"][0]: 20},
                ]
            }

        mock_cloudwatch.get_metric_statistics = MagicMock(side_effect=side_effect)
        mock_boto3.return_value = mock_cloudwatch
        expected = {
            "service_a:test_metric_1:test_label_1": 30.0,
            "service_b:test_metric_1:test_label_1": 30.0,
            "service_c:test_metric_2": 15.0,
        }

        loader = AwsMetricsLoader(
            "ap0",
            "us-west-1",
            datetime.fromisoformat("2024-12-12"),
            datetime.fromisoformat("2024-12-13"),
        )
        self.assertEqual(expected, loader.get_metrics(cost_yaml))


class TestAwsAggregator(unittest.TestCase):
    """Tests for the AwsAggregator."""

    def setUp(self):
        self.aggregator = AwsAggregator()

    def test_aggregate_sum(self):
        request = {"aggregation": "Sum"}
        result = {"Datapoints": [{"Sum": 10}, {"Sum": 20}, {"Sum": 30}]}
        self.assertEqual(self.aggregator.aggregate(request, result), 60)

    def test_aggregate_average(self):
        request = {"aggregation": "Average"}
        result = {"Datapoints": [{"Average": 10}, {"Average": 20}, {"Average": 30}]}
        self.assertEqual(self.aggregator.aggregate(request, result), 20)

    def test_aggregate_no_datapoints(self):
        request = {"aggregation": "Sum"}
        result = {"Datapoints": []}
        self.assertEqual(self.aggregator.aggregate(request, result), 0)


if __name__ == "__main__":
    unittest.main()
