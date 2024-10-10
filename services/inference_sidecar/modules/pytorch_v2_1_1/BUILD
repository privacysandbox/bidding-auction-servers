# Copyright 2024 Google LLC
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

load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "inference_sidecar_bin",
    srcs = ["@inference_common//:inference_sidecar_main.cc"],
    malloc = select({
        ":use_tcmalloc": "@com_google_tcmalloc//tcmalloc",
        "//conditions:default": "@bazel_tools//tools/cpp:malloc",
    }),
    deps = [
        ":pytorch",
        "@inference_common//:grpc_sidecar",
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
    name = "test_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_simple_model.pt"],
    outs = ["test_model"],
    cmd = "cp $< $@",
)

cc_test(
    name = "inference_sidecar_test",
    size = "medium",
    timeout = "short",
    srcs = ["@inference_common//:inference_sidecar_test.cc"],
    data = [
        ":inference_sidecar_test_target",
        ":test_model_target",
    ],
    flaky = True,
    deps = [
        ":test_constants",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:reflection",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@inference_common//proto:inference_sidecar_cc_grpc_proto",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//sandbox:sandbox_executor",
        "@inference_common//utils:file_util",
    ],
)

cc_library(
    name = "test_constants",
    testonly = True,
    hdrs = ["test_constants.h"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "pytorch",
    srcs = ["pytorch.cc"],
    hdrs = ["pytorch.h"],
    deps = [
        ":pytorch_parser",
        ":validator",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@inference_common//model:model_store",
        "@inference_common//modules:module_interface",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:inference_error_code",
        "@inference_common//utils:inference_metric_util",
        "@inference_common//utils:log",
        "@inference_common//utils:request_parser",
        "@pytorch_v2_1_1//:torch",
    ],
)

genrule(
    name = "test_simple_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_simple_model.pt"],
    outs = ["simple_model"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_e2e_model1_target",
    srcs = ["@inference_common//testdata:models/pytorch_e2e_model.pt"],
    outs = ["e2e_model1"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_e2e_model2_target",
    srcs = ["@inference_common//testdata:models/pytorch_e2e_model.pt"],
    outs = ["e2e_model2"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_stateful_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_stateful_model.pt"],
    outs = ["stateful_model"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_unfreezable_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_unfreezable_model.pt"],
    outs = ["unfreezable_model"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_frozen_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_frozen_model.pt"],
    outs = ["frozen_model"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_mixed_inputs_mixed_outputs_target",
    srcs = ["@inference_common//testdata:models/pytorch_mixed_inputs_mixed_outputs_model.pt"],
    outs = ["mixed_inputs_mixed_outputs_model"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_submodule_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_submodule_model.pt"],
    outs = ["submodule_model"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_external_function_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_external_function_model.pt"],
    outs = ["external_function_model"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_external_class_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_external_class_model.pt"],
    outs = ["external_class_model"],
    cmd = "cp $< $@",
)

genrule(
    name = "test_nested_block_model_target",
    srcs = ["@inference_common//testdata:models/pytorch_nested_block_model.pt"],
    outs = ["nested_block_model"],
    cmd = "cp $< $@",
)

cc_test(
    name = "pytorch_test",
    size = "small",
    srcs = ["pytorch_test.cc"],
    data = [
        ":test_e2e_model1_target",
        ":test_e2e_model2_target",
        ":test_frozen_model_target",
        ":test_mixed_inputs_mixed_outputs_target",
        ":test_simple_model_target",
        ":test_stateful_model_target",
        ":test_unfreezable_model_target",
    ],
    deps = [
        ":pytorch",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:file_util",
        "@inference_common//utils:inference_metric_util",
        "@inference_common//utils:test_util",
        "@pytorch_v2_1_1//:torch",
    ],
)

cc_library(
    name = "pytorch_parser",
    srcs = ["pytorch_parser.cc"],
    hdrs = [
        "pytorch_parser.h",
    ],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:error",
        "@inference_common//utils:request_parser",
        "@pytorch_v2_1_1//:torch",
    ],
)

cc_test(
    name = "pytorch_parser_test",
    size = "small",
    srcs = ["pytorch_parser_test.cc"],
    deps = [
        ":pytorch_parser",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:error",
    ],
)

cc_library(
    name = "validator",
    srcs = ["validator.cc"],
    hdrs = [
        "validator.h",
    ],
    deps = [
        "@com_google_absl//absl/log",
        "@inference_common//model:validator",
        "@pytorch_v2_1_1//:torch",
    ],
)

cc_test(
    name = "validator_test",
    size = "small",
    srcs = ["validator_test.cc"],
    data = [
        ":test_external_class_model_target",
        ":test_external_function_model_target",
        ":test_nested_block_model_target",
        ":test_simple_model_target",
        ":test_stateful_model_target",
        ":test_submodule_model_target",
    ],
    deps = [
        ":validator",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@pytorch_v2_1_1//:torch",
    ],
)

cc_test(
    name = "module_concurrency_test",
    size = "small",
    srcs = ["@inference_common//modules:module_concurrency_test.cc"],
    data = [
        ":test_model_target",
    ],
    malloc = select({
        ":use_tcmalloc": "@com_google_tcmalloc//tcmalloc",
        "//conditions:default": "@bazel_tools//tools/cpp:malloc",
    }),
    deps = [
        ":pytorch",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@inference_common//benchmark:request_utils",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:file_util",
    ],
)

string_flag(
    name = "tcmalloc_build",
    build_setting_default = "no",
    values = [
        "yes",
        "no",
    ],
)

config_setting(
    name = "use_tcmalloc",
    flag_values = {
        ":tcmalloc_build": "yes",
    },
    visibility = ["//visibility:public"],
)

genrule(
    name = "generate_artifacts",
    srcs = [
        ":inference_sidecar_bin",
    ],
    outs = ["inference_sidecar_binary"],
    cmd_bash = """cat << EOF > '$@'
mkdir -p artifacts
cp $(execpath :inference_sidecar_bin) artifacts/inference_sidecar_pytorch_v2_1_1
EOF""",
    executable = True,
    local = True,
    message = "generate inference sidecar artifacts",
)

# Exports PyTorch inference sidecar binary to be packaged in the B&A workspace.
exports_files(
    [
        "artifacts/inference_sidecar_pytorch_v2_1_1",
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
