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
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
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
        "http_fetcher_async.h",
        "multi_curl_http_fetcher_async.h",
    ],
    deps = [
        "//services/common/loggers:request_log_context",
        "@com_google_absl//absl/container:flat_hash_map",
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
