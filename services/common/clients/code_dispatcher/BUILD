# Copyright 2023 Google LLC
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

cc_library(
    name = "udf_code_loader_interface",
    hdrs = ["udf_code_loader_interface.h"],
    deps = [
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "v8_dispatcher",
    srcs = ["v8_dispatcher.cc"],
    hdrs = ["v8_dispatcher.h"],
    deps = [
        ":request_context",
        ":udf_code_loader_interface",
        "//services/common/loggers:request_log_context",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/roma/roma_service",
    ],
)

cc_library(
    name = "v8_dispatch_client",
    srcs = ["v8_dispatch_client.cc"],
    hdrs = ["v8_dispatch_client.h"],
    deps = [
        ":v8_dispatcher",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "v8_dispatch_client_test",
    size = "small",
    srcs = ["v8_dispatch_client_test.cc"],
    deps = [
        ":v8_dispatch_client",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "v8_dispatch_client_integration_test",
    size = "large",
    srcs = ["v8_dispatch_client_integration_test.cc"],
    deps = [
        ":v8_dispatch_client",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "request_context",
    hdrs = [
        "request_context.h",
    ],
    deps = [
        "//services/common/loggers:request_log_context",
        "//services/common/metric:server_definition",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/status:statusor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)
