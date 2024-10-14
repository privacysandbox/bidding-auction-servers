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

package(default_visibility = ["//visibility:public"])

# We export the test files where different inference backends can run the test in their respective workspaces.
exports_files([
    "module_concurrency_test.cc",
])

cc_library(
    name = "test_module",
    srcs = ["test_module.cc"],
    hdrs = ["test_module.h"],
    deps = [
        ":module_interface",
        "//:test_constants",
        "//proto:inference_sidecar_cc_proto",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/util:runfiles",
    ],
)

cc_test(
    name = "test_module_test",
    size = "small",
    srcs = ["test_module_test.cc"],
    data = [
        "//testdata:models/tensorflow_1_mib_saved_model.pb",
    ],
    deps = [
        ":test_module",
        "//proto:inference_sidecar_cc_proto",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "module_interface",
    hdrs = [
        "module_interface.h",
    ],
    deps = [
        "//proto:inference_sidecar_cc_proto",
        "//utils:log",
        "@com_google_absl//absl/status:statusor",
    ],
)

# TODO(b/327860817): Enable ASAN/TSAN continuous tests for inference.
cc_test(
    name = "module_concurrency_test",
    size = "small",
    srcs = ["module_concurrency_test.cc"],
    data = [
        "//:gen_test_model",
    ],
    deps = [
        ":test_module",
        "//benchmark:request_utils",
        "//proto:inference_sidecar_cc_proto",
        "//utils:file_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
