# Copyright 2025 Google LLC
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
    name = "blob_storage_client_cpio",
    srcs = ["blob_storage_client_cpio.cc"],
    hdrs = [
        "blob_storage_client.h",
        "blob_storage_client_cpio.h",
    ],
    deps = [
        ":blob_storage_client_cpio_utils",
        "//services/common/clients/config:parc_service_grpc_proto",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/core/interface:async_context",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_library(
    name = "blob_storage_client_cpio_utils",
    srcs = ["blob_storage_client_cpio_utils.cc"],
    hdrs = [
        "blob_storage_client.h",
        "blob_storage_client_cpio_utils.h",
    ],
    deps = [
        "//services/common/clients/config:parc_service_grpc_proto",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/core/interface:async_context",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_test(
    name = "blob_storage_client_cpio_test",
    size = "small",
    srcs = ["blob_storage_client_cpio_test.cc"],
    deps = [
        ":blob_storage_client_cpio",
        ":blob_storage_client_cpio_utils",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
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

cc_library(
    name = "blob_storage_client_parc",
    srcs = ["blob_storage_client_parc.cc"],
    hdrs = [
        "blob_storage_client.h",
        "blob_storage_client_parc.h",
    ],
    deps = [
        ":blob_storage_client_parc_utils",
        "//services/common/clients/config:parc_service_grpc_proto",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_library(
    name = "blob_storage_client_parc_utils",
    srcs = ["blob_storage_client_parc_utils.cc"],
    hdrs = [
        "blob_storage_client.h",
        "blob_storage_client_parc_utils.h",
    ],
    deps = [
        "//services/common/clients/config:parc_service_grpc_proto",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/core/interface:async_context",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_test(
    name = "blob_storage_client_parc_test",
    size = "small",
    srcs = ["blob_storage_client_parc_test.cc"],
    deps = [
        ":blob_storage_client_parc",
        ":blob_storage_client_parc_utils",
        "//services/common/clients/config:parc_service_grpc_proto",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/core/test/utils",
    ],
)
