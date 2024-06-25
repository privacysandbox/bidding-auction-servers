# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//:config.bzl", "IS_PROD_BUILD_DEFINES")

cc_library(
    name = "inference_utils",
    srcs = [
        "inference_utils.cc",
    ],
    hdrs = [
        "inference_utils.h",
    ],
    local_defines = IS_PROD_BUILD_DEFINES,
    visibility = ["//visibility:public"],
    deps = [
        ":inference_flags",
        "//services/common/blob_fetch:blob_fetcher",
        "//services/common/clients/code_dispatcher:request_context",
        "//services/common/util:request_response_constants",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@google_privacysandbox_servers_common//src/roma/roma_service",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
        "@inference_common//proto:inference_sidecar_cc_grpc_proto",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//sandbox:sandbox_executor",
        "@inference_common//utils:file_util",
        "@rapidjson",
    ],
)

cc_test(
    name = "inference_utils_test",
    size = "small",
    srcs = ["inference_utils_test.cc"],
    data = [
        "@inference_common//:inference_sidecar",
        "@inference_common//testdata:models/tensorflow_1_mib_saved_model.pb",
    ],
    flaky = True,
    deps = [
        ":inference_flags",
        ":inference_utils",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:reflection",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/roma/roma_service",
    ],
)

cc_library(
    name = "inference_flags",
    srcs = [
        "inference_flags.cc",
    ],
    hdrs = [
        "inference_flags.h",
    ],
    visibility = [
        "//services/bidding_service:__subpackages__",
    ],
    deps = [
        "@com_google_absl//absl/flags:flag",
    ],
)
