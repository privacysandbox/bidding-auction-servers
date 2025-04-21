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
    name = "http_fetcher_async",
    hdrs = [
        "http_fetcher_async.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":curl_request_data",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "curl_request_data",
    srcs = [
        "curl_request_data.cc",
    ],
    hdrs = [
        "curl_request_data.h",
    ],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@curl",
        "@libevent//:event",
    ],
)

cc_library(
    name = "curl_request_queue",
    srcs = [
        "curl_request_queue.cc",
    ],
    hdrs = [
        "curl_request_queue.h",
    ],
    deps = [
        ":curl_request_data",
        "//services/common/cache:doubly_linked_list",
        "//services/common/util:event",
        "//services/common/util:event_base",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
    ],
)

cc_library(
    name = "curl_request_worker",
    srcs = [
        "curl_request_worker.cc",
    ],
    hdrs = [
        "curl_request_worker.h",
    ],
    deps = [
        ":curl_request_data",
        ":curl_request_queue",
        ":multi_curl_request_manager",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
    ],
)

cc_library(
    name = "multi_curl_request_manager",
    srcs = [
        "multi_curl_request_manager.cc",
    ],
    hdrs = [
        "multi_curl_request_manager.h",
    ],
    deps = [
        ":curl_request_data",
        ":http_fetcher_async",
        "//services/common/loggers:request_log_context",
        "//services/common/util:event",
        "//services/common/util:event_base",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@curl",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@libevent//:event",
    ],
)

cc_library(
    name = "multi_curl_http_fetcher_async",
    srcs = [
        "multi_curl_http_fetcher_async.cc",
        "multi_curl_request_manager.cc",
        "multi_curl_request_manager.h",
    ],
    hdrs = [
        "multi_curl_http_fetcher_async.h",
    ],
    deps = [
        ":curl_request_data",
        ":curl_request_queue",
        ":curl_request_worker",
        ":http_fetcher_async",
        "//services/common/cache:doubly_linked_list",
        "//services/common/constants:common_service_flags",
        "//services/common/loggers:request_log_context",
        "//services/common/util:file_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@curl",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@libevent//:event",
    ],
)

cc_library(
    name = "multi_curl_http_fetcher_async_no_queue",
    srcs = [
        "multi_curl_http_fetcher_async_no_queue.cc",
        "multi_curl_request_manager_locking.cc",
        "multi_curl_request_manager_locking.h",
    ],
    hdrs = [
        "multi_curl_http_fetcher_async_no_queue.h",
    ],
    deps = [
        ":curl_request_data",
        ":http_fetcher_async",
        "//services/common/constants:common_service_flags",
        "//services/common/loggers:request_log_context",
        "//services/common/util:event",
        "//services/common/util:event_base",
        "//services/common/util:file_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@curl",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@libevent//:event",
    ],
)

cc_test(
    name = "multi_curl_http_fetcher_async_test",
    size = "medium",
    srcs = ["multi_curl_http_fetcher_async_test.cc"],
    tags = [
        "flaky",
        "requires-network",
    ],
    deps = [
        ":multi_curl_http_fetcher_async",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@rapidjson",
    ],
)

cc_test(
    name = "curl_request_queue_test",
    size = "small",
    srcs = ["curl_request_queue_test.cc"],
    deps = [
        ":curl_request_data",
        ":curl_request_queue",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@rapidjson",
    ],
)

cc_test(
    name = "curl_request_worker_test",
    size = "small",
    srcs = ["curl_request_worker_test.cc"],
    deps = [
        ":curl_request_data",
        ":curl_request_queue",
        ":curl_request_worker",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@rapidjson",
    ],
)
