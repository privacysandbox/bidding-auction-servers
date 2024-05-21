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

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "sandbox_benchmark",
    srcs = [
        "sandbox_benchmark.cc",
    ],
    data = [
        ":inference_sidecar_ipc_test_target",
        "//:gen_test_model",
        "//:inference_sidecar_test_target",
    ],
    deps = [
        "//proto:inference_sidecar_cc_grpc_proto",
        "//proto:inference_sidecar_cc_proto",
        "//sandbox:sandbox_executor",
        "//utils:file_util",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_benchmark//:benchmark",
        "@com_google_benchmark//:benchmark_main",
    ],
)

# Builds the IPC inference sidecar for testing.
cc_binary(
    name = "ipc_inference_sidecar_test_bin",
    srcs = ["ipc_sidecar_main.cc"],
    deps = [
        "//:ipc_sidecar",
        "//modules:test_module",
        "@com_google_absl//absl/log:check",
    ],
)

genrule(
    name = "inference_sidecar_ipc_test_target",
    srcs = [":ipc_inference_sidecar_test_bin"],
    outs = ["ipc_inference_sidecar"],
    cmd = "cp $< $@",
)
