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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "blob_fetcher",
    srcs = ["blob_fetcher.cc"],
    hdrs = ["blob_fetcher.h"],
    deps = [
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
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
