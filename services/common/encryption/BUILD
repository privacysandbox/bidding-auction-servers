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

package(
    default_visibility = [
        "//services:__subpackages__",
    ],
    licenses = ["notice"],
)

cc_library(
    name = "key_fetcher_factory",
    srcs = [
        "key_fetcher_factory.cc",
    ],
    hdrs = [
        "key_fetcher_factory.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/clients/config:config_client",
        "//services/common/constants:common_service_flags",
        "@com_github_grpc_grpc//:grpc++",
        "@google_privacysandbox_servers_common//src/cpp/concurrent:executor",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/src:fake_key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/src:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/src:private_key_fetcher",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/src:public_key_fetcher",
    ],
)

cc_library(
    name = "crypto_client_wrapper_interface",
    srcs = [
        "crypto_client_wrapper_interface.h",
    ],
    deps = [
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/status:statusor",
        "@control_plane_shared//cc/public/cpio/interface/crypto_client",
        "@control_plane_shared//cc/public/cpio/proto/crypto_service/v1:crypto_service_cc_proto",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/interface:private_key_fetcher_interface",
    ],
)

cc_library(
    name = "crypto_client_wrapper",
    srcs = [
        "crypto_client_wrapper.cc",
    ],
    hdrs = [
        "crypto_client_wrapper.h",
        "crypto_client_wrapper_interface.h",
    ],
    deps = [
        ":crypto_client_wrapper_interface",
        "//services/common/util:status_macros",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/status:statusor",
        "@control_plane_shared//cc/public/cpio/interface/crypto_client",
        "@control_plane_shared//cc/public/cpio/proto/crypto_service/v1:crypto_service_cc_proto",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/interface:private_key_fetcher_interface",
    ],
)

cc_test(
    name = "crypto_client_wrapper_test",
    srcs = [
        "crypto_client_wrapper_test.cc",
    ],
    deps = [
        ":crypto_client_wrapper",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "crypto_client_factory",
    srcs = [
        "crypto_client_factory.cc",
    ],
    hdrs = [
        "crypto_client_factory.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":crypto_client_wrapper",
    ],
)

cc_library(
    name = "mock_crypto_client_wrapper",
    srcs = [
        "mock_crypto_client_wrapper.h",
    ],
    deps = [
        ":crypto_client_wrapper",
        ":crypto_client_wrapper_interface",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
