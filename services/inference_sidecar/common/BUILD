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

# We export inference_sidecar's main and test files. Inference sidecar binary
# with different inference backends need to be built in their respective
# workspaces.
exports_files([
    "inference_sidecar_main.cc",
    "inference_sidecar_test.cc",
])

cc_library(
    name = "ipc_sidecar",
    srcs = ["ipc_sidecar.cc"],
    hdrs = ["ipc_sidecar.h"],
    deps = [
        "//modules:module_interface",
        "//proto:inference_sidecar_cc_proto",
        "//sandbox:sandbox_worker",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_library(
    name = "grpc_sidecar",
    srcs = ["grpc_sidecar.cc"],
    hdrs = ["grpc_sidecar.h"],
    deps = [
        "//modules:module_interface",
        "//proto:inference_sidecar_cc_grpc_proto",
        "//proto:inference_sidecar_cc_proto",
        "//sandbox:sandbox_worker",
        "//utils:cpu",
        "//utils:log",
        "//utils:tcmalloc",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

# Builds inference sidecar using test module.
cc_binary(
    name = "inference_sidecar_bin",
    srcs = ["inference_sidecar_main.cc"],
    deps = [
        ":grpc_sidecar",
        "//modules:test_module",
        "@com_google_absl//absl/log:check",
    ],
)

# Generate inference sidecar as "inference_sidecar" during test time.
genrule(
    name = "inference_sidecar_test_target",
    srcs = [":inference_sidecar_bin"],
    outs = ["inference_sidecar"],
    cmd = "cp $< $@",
)

# Rename a specific test model as "test_model" so that the inference_sidecar_test.cc
# can have a single hardcoded string to refer to the test model for various
# inference backends.
genrule(
    name = "gen_test_model",
    srcs = ["//testdata:models/tensorflow_1_mib_saved_model.pb"],
    outs = ["test_model"],
    cmd = "cp $< $@",
)

cc_test(
    name = "inference_sidecar_test",
    size = "medium",
    timeout = "short",
    srcs = ["inference_sidecar_test.cc"],
    data = [
        ":gen_test_model",
        ":inference_sidecar_test_target",
    ],
    flaky = True,
    deps = [
        ":test_constants",
        "//proto:inference_sidecar_cc_grpc_proto",
        "//proto:inference_sidecar_cc_proto",
        "//sandbox:sandbox_executor",
        "//utils:file_util",
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

cc_test(
    name = "consented_logging_test",
    srcs = ["consented_logging_test.cc"],
    data = [
        ":gen_test_model",
        ":inference_sidecar_test_target",
    ],
    deps = [
        ":test_constants",
        "//proto:inference_sidecar_cc_grpc_proto",
        "//proto:inference_sidecar_cc_proto",
        "//sandbox:sandbox_executor",
        "//utils:file_util",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:reflection",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

# Used to faciliate setting common constants in both inside the (test module)
# inference sidecar and the test that invokes the sidecar. As a result, this
# library cannot be marked as testonly.
cc_library(
    name = "test_constants",
    hdrs = ["test_constants.h"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

genrule(
    name = "collect-logs",
    outs = ["collect_logs.bin"],
    cmd_bash = """cat << EOF > '$@'
tools/collect-logs "\\$$@"
EOF""",
    executable = True,
    local = True,
    message = "copy bazel build and test logs",
)
