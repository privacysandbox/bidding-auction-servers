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

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "sandbox_worker",
    srcs = ["sandbox_worker.cc"],
    hdrs = ["sandbox_worker.h"],
    deps = [
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
    ],
)

cc_library(
    name = "sandbox_executor",
    srcs = ["sandbox_executor.cc"],
    hdrs = ["sandbox_executor.h"],
    deps = [
        ":sandbox_worker",
        "//utils:resource_size_utils",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:runfiles",
    ],
)

cc_test(
    name = "sandbox_executor_test",
    size = "small",
    srcs = ["sandbox_executor_test.cc"],
    data = [
        ":sandboxee_exit_test_bin",
        ":sandboxee_grpc_test_bin",
        ":sandboxee_ipc_test_bin",
    ],
    flaky = True,
    deps = [
        ":sandbox_executor",
        "//proto:testproto_cc_grpc_proto",
        "//proto:testproto_cc_proto",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:reflection",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_binary(
    name = "sandboxee_exit_test_bin",
    testonly = 1,
    srcs = ["sandboxee_exit_test_bin.cc"],
    features = [
        "-pie",
        "fully_static_link",
    ],
    deps = [
        ":sandbox_worker",
    ],
)

cc_binary(
    name = "sandboxee_ipc_test_bin",
    testonly = 1,
    srcs = ["sandboxee_ipc_test_bin.cc"],
    features = [
        "-pie",
        "fully_static_link",
    ],
    deps = [
        ":sandbox_worker",
        "//proto:testproto_cc_proto",
        "@com_google_absl//absl/log:absl_log",
    ],
)

cc_binary(
    name = "sandboxee_grpc_test_bin",
    testonly = 1,
    srcs = ["sandboxee_grpc_test_bin.cc"],
    features = [
        "-pie",
        "fully_static_link",
    ],
    deps = [
        ":sandbox_worker",
        "//proto:testproto_cc_grpc_proto",
        "//proto:testproto_cc_proto",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/log:absl_log",
    ],
)
