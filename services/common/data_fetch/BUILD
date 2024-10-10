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
    name = "fetcher_interface",
    hdrs = ["fetcher_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "periodic_url_fetcher",
    srcs = ["periodic_url_fetcher.cc"],
    hdrs = ["periodic_url_fetcher.h"],
    deps = [
        ":fetcher_interface",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
    ],
)

cc_library(
    name = "periodic_code_fetcher",
    srcs = ["periodic_code_fetcher.cc"],
    hdrs = ["periodic_code_fetcher.h"],
    deps = [
        ":periodic_url_fetcher",
        "//services/common/clients/code_dispatcher:udf_code_loader_interface",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
    ],
)

cc_library(
    name = "periodic_bucket_fetcher",
    srcs = ["periodic_bucket_fetcher.cc"],
    hdrs = ["periodic_bucket_fetcher.h"],
    deps = [
        ":fetcher_interface",
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_library(
    name = "periodic_bucket_code_fetcher",
    srcs = ["periodic_bucket_code_fetcher.cc"],
    hdrs = ["periodic_bucket_code_fetcher.h"],
    deps = [
        ":periodic_bucket_fetcher",
        "//services/common/clients/code_dispatcher:udf_code_loader_interface",
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_test(
    name = "periodic_code_fetcher_test",
    size = "small",
    srcs = ["periodic_code_fetcher_test.cc"],
    deps = [
        ":fetcher_interface",
        ":periodic_code_fetcher",
        "//services/common/clients/code_dispatcher:udf_code_loader_interface",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
    ],
)

cc_test(
    name = "periodic_bucket_code_fetcher_test",
    size = "small",
    srcs = ["periodic_bucket_code_fetcher_test.cc"],
    deps = [
        ":fetcher_interface",
        ":periodic_bucket_code_fetcher",
        "//services/common/clients/code_dispatcher:udf_code_loader_interface",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/core/interface:async_context",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface:cpio",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/blob_storage_client:blob_storage_client_mock",
    ],
)
