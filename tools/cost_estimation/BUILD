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

load("@cost_estimation_deps//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

py_binary(
    name = "main",
    srcs = ["main.py"],
    data = [
        "cost.yaml",
        "sku.json",
    ],
    deps = [
        "estimator",
        "render",
        ":cost_tool",
        ":sql_expr",
    ],
)

py_library(
    name = "cost_tool",
    srcs = ["cost_tool.py"],
    deps = [
        "estimator",
        "render",
        ":sql_expr",
        requirement("PyYAML"),
    ],
)

py_test(
    name = "cost_tool_test",
    size = "small",
    srcs = ["cost_tool_test.py"],
    deps = [
        ":cost_tool",
    ],
)

py_library(
    name = "sql_expr",
    srcs = ["sql_expr.py"],
    deps = [],
)

py_test(
    name = "sql_expr_test",
    size = "small",
    srcs = ["sql_expr_test.py"],
    deps = [
        ":sql_expr",
    ],
)

py_library(
    name = "metrics_loader",
    srcs = ["metrics_loader.py"],
    deps = [
        ":utils",
        requirement("google-cloud-monitoring"),
        requirement("grpcio"),
        requirement("boto3"),
    ],
)

py_test(
    name = "metrics_loader_test",
    size = "small",
    srcs = ["metrics_loader_test.py"],
    deps = [
        ":metrics_loader",
    ],
)

py_library(
    name = "estimator",
    srcs = ["estimator.py"],
    deps = [
        "metrics_loader",
        ":sql_expr",
        ":utils",
    ],
)

py_test(
    name = "estimator_test",
    size = "small",
    srcs = ["estimator_test.py"],
    deps = [
        ":estimator",
        ":sql_expr",
    ],
)

py_library(
    name = "render",
    srcs = ["render.py"],
    deps = [
        ":sql_expr",
    ],
)

py_test(
    name = "render_test",
    size = "small",
    srcs = ["render_test.py"],
    deps = [
        ":estimator",
        ":render",
        ":sql_expr",
    ],
)

py_library(
    name = "utils",
    srcs = ["utils.py"],
    deps = [
    ],
)

py_test(
    name = "utils_test",
    size = "small",
    srcs = ["utils_test.py"],
    deps = [
        ":utils",
    ],
)
