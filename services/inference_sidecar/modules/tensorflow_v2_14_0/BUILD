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

load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load(
    "@rules_pkg//pkg:mappings.bzl",
    "pkg_attributes",
    "pkg_files",
    "strip_prefix",
)
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "inference_sidecar_bin",
    srcs = ["@inference_common//:inference_sidecar_main.cc"],
    malloc = select({
        ":use_tcmalloc": "@com_google_tcmalloc//tcmalloc",
        "//conditions:default": "@bazel_tools//tools/cpp:malloc",
    }),
    deps = [
        ":tensorflow",
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

# Copy a set of model files to "test_model" directory so that
# inference_sidecar_test.cc can pass the file path to the TensorFlow
# inference sidecar.
genrule(
    name = "test_model_target",
    srcs = [":test_model_zip"],
    outs = ["test_model"],
    cmd = "mkdir -p $(OUTS) && unzip $(SRCS) -d $(OUTS)",
)

pkg_zip(
    name = "test_model_zip",
    srcs = ["test_model_files"],
)

pkg_files(
    name = "test_model_files",
    srcs = ["@inference_common//testdata/models/tensorflow_simple_model:test_model"],
    attributes = pkg_attributes(mode = "0555"),
    strip_prefix = strip_prefix.from_pkg(),
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
    name = "tensorflow_parser",
    srcs = ["tensorflow_parser.cc"],
    hdrs = [
        "tensorflow_parser.h",
    ],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:error",
        "@inference_common//utils:request_parser",
        "@org_tensorflow//tensorflow/core:framework",
    ],
)

cc_test(
    name = "tensorflow_parser_test",
    size = "small",
    srcs = ["tensorflow_parser_test.cc"],
    deps = [
        ":tensorflow_parser",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@inference_common//proto:inference_sidecar_cc_proto",
    ],
)

cc_library(
    name = "tensorflow",
    srcs = ["tensorflow.cc"],
    hdrs = ["tensorflow.h"],
    deps = [
        ":tensorflow_parser",
        ":validator",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@inference_common//model:model_store",
        "@inference_common//modules:module_interface",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:error",
        "@inference_common//utils:inference_error_code",
        "@inference_common//utils:inference_metric_util",
        "@inference_common//utils:request_parser",
        "@org_tensorflow//tensorflow/cc:cc_ops",
        "@org_tensorflow//tensorflow/cc:client_session",
        "@org_tensorflow//tensorflow/cc:ops",
        "@org_tensorflow//tensorflow/cc/saved_model:constants",
        "@org_tensorflow//tensorflow/cc/saved_model:loader",
        "@org_tensorflow//tensorflow/cc/saved_model:signature_constants",
        "@org_tensorflow//tensorflow/cc/saved_model:tag_constants",
        "@org_tensorflow//tensorflow/cc/tools:freeze_saved_model",
        "@org_tensorflow//tensorflow/core:core_cpu",
        "@org_tensorflow//tensorflow/core:example_parser_configuration",
        "@org_tensorflow//tensorflow/core:framework",
        "@org_tensorflow//tensorflow/core:lib",
        "@org_tensorflow//tensorflow/core:protos_all_cc",
        "@org_tensorflow//tensorflow/core/profiler/lib:traceme",
        "@org_tensorflow//tensorflow/tsl/platform:env",
    ],
)

cc_test(
    name = "tensorflow_test",
    size = "small",
    srcs = ["tensorflow_test.cc"],
    data = [
        "//benchmark_models/embedding:embedding_model",
        "//benchmark_models/frozen_embedding:frozen_embedding_model",
        "//benchmark_models/frozen_pctr:frozen_pctr_model",
        "//benchmark_models/frozen_pcvr:frozen_pcvr_model",
        "//benchmark_models/pctr:pctr_model",
        "//benchmark_models/pcvr:pcvr_model",
        "//benchmark_models/stateful:stateful_model",
    ],
    deps = [
        ":tensorflow",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:file_util",
        "@inference_common//utils:inference_metric_util",
        "@inference_common//utils:test_util",
        "@org_tensorflow//tensorflow/core:protos_all_cc",
        "@org_tensorflow//tensorflow/core/profiler/lib:traceme",
    ],
)

cc_test(
    name = "ram_file_system_test",
    size = "small",
    srcs = ["ram_file_system_test.cc"],
    data = [
        ":test_model_target",
    ],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@org_tensorflow//tensorflow/cc/saved_model:constants",
        "@org_tensorflow//tensorflow/cc/saved_model:loader",
        "@org_tensorflow//tensorflow/cc/saved_model:signature_constants",
        "@org_tensorflow//tensorflow/cc/saved_model:tag_constants",
        "@org_tensorflow//tensorflow/core:framework",
        "@org_tensorflow//tensorflow/core:lib",
        "@org_tensorflow//tensorflow/tsl/platform:env",
    ],
)

cc_library(
    name = "validator",
    srcs = ["validator.cc"],
    hdrs = [
        "validator.h",
    ],
    deps = [
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:absl_log",
        "@inference_common//model:validator",
        "@org_tensorflow//tensorflow/cc:client_session",
        "@org_tensorflow//tensorflow/core:framework",
    ],
)

cc_test(
    name = "validator_test",
    srcs = ["validator_test.cc"],
    data = [
        "//benchmark_models/stateful:stateful_model",
    ],
    deps = [
        ":validator",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@org_tensorflow//tensorflow/cc:client_session",
        "@org_tensorflow//tensorflow/cc/saved_model:loader",
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
        ":tensorflow",
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
cp $(execpath :inference_sidecar_bin) artifacts/inference_sidecar_tensorflow_v2_14_0
EOF""",
    executable = True,
    local = True,
    message = "generate inference sidecar artifacts",
)

# Exports TensorFlow inference sidecar binary to be packaged in the B&A workspace.
exports_files(
    [
        "artifacts/inference_sidecar_tensorflow_v2_14_0",
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
