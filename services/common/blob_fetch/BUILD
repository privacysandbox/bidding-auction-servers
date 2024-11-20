# Copyright 2024 Google LLC
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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_proto_library", "cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "blob_fetcher_base",
    hdrs = ["blob_fetcher_base.h"],
    deps = [
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "blob_fetcher_mock",
    testonly = 1,
    hdrs = ["blob_fetcher_mock.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":blob_fetcher_base",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "blob_fetcher",
    srcs = ["blob_fetcher.cc"],
    hdrs = ["blob_fetcher.h"],
    deps = [
        ":blob_fetcher_base",
        "//services/common/loggers:request_log_context",
        "//services/common/util:hash_util",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "blob_fetcher_test",
    size = "small",
    srcs = ["blob_fetcher_test.cc"],
    deps = [
        ":blob_fetcher",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/blob_storage_client:blob_storage_client_mock",
    ],
)

proto_library(
    name = "fetch_mode_proto",
    srcs = ["fetch_mode.proto"],
    deps = [
        "@com_google_googleapis//google/api:annotations_proto",
    ],
)

cc_proto_library(
    name = "fetch_mode_cc_proto",
    deps = [":fetch_mode_proto"],
)
