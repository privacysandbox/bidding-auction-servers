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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//:__subpackages__"])
# Description:
#   grpc clients for Fledge Key Value Server
#   https://github.com/WICG/turtledove/blob/main/FLEDGE_Key_Value_Server_API.md

cc_library(
    name = "async_client",
    hdrs = ["async_client.h"],
    # header only library for interface
    linkstatic = True,
    deps = [
        ":client_params_template",
        "//services/common/clients/async_grpc:request_config",
        "//services/common/loggers:request_log_context",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "client_params_template",
    hdrs = ["client_params.h"],
    # header only library for interface
    linkstatic = True,
    deps = [
        "//services/common/clients/async_grpc:request_config",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_library(
    name = "client_factory_template",
    hdrs = ["client_factory.h"],
    # header only library for interface
    linkstatic = True,
    deps = [
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_test(
    name = "client_params_test",
    size = "small",
    srcs =
        [
            "client_params_test.cc",
        ],
    deps = [
        ":client_params_template",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)
