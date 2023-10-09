# Copyright 2022 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "score_ads_benchmarking_logger",
    hdrs = ["score_ads_benchmarking_logger.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/loggers:benchmarking_logger",
        "//services/common/loggers:build_input_process_response_benchmarking_logger",
        "//services/common/loggers:timer",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "score_ads_no_op_logger",
    hdrs = ["score_ads_no_op_logger.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":score_ads_benchmarking_logger",
        "//services/common/loggers:timer",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)
