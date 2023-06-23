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
    name = "timer",
    srcs = ["timer.cc"],
    hdrs = ["timer.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "benchmarking_logger",
    srcs = ["benchmarking_logger.cc"],
    hdrs = [
        "benchmarking_logger.h",
        "no_ops_logger.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/loggers:timer",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "build_input_process_response_benchmarking_logger",
    srcs = ["build_input_process_response_benchmarking_logger.cc"],
    hdrs = ["build_input_process_response_benchmarking_logger.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":benchmarking_logger",
        "@com_google_absl//absl/strings",
    ],
)
