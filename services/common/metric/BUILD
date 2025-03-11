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

package(default_visibility = ["//visibility:private"])

cc_library(
    name = "error_code",
    hdrs = ["error_code.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "server_definition",
    hdrs = [
        "server_definition.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":error_code",
        "//services/common/util:read_system",
        "//services/common/util:reporting_util",
        "//services/seller_frontend_service/k_anon:constants",
        "@google_privacysandbox_servers_common//src/metric:context_map",
        "@google_privacysandbox_servers_common//src/metric:key_fetch",
        "@inference_common//utils:inference_error_code",
    ],
)

cc_library(
    name = "udf_metric",
    hdrs = [
        "udf_metric.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/clients/code_dispatcher:request_context",
        "//services/common/loggers:request_log_context",
        "@google_privacysandbox_servers_common//src/roma/roma_service",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_test(
    name = "server_definition_test",
    timeout = "short",
    srcs = ["server_definition_test.cc"],
    deps = [
        ":server_definition",
        "@com_google_googletest//:gtest_main",
    ],
)
