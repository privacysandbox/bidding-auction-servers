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

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "module_benchmark",
    srcs = ["@inference_common//benchmark:module_benchmark.cc"],
    data = [
        "//:test_model_target",
    ],
    deps = [
        "//:tensorflow",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_benchmark//:benchmark",
        "@com_google_benchmark//:benchmark_main",
        "@inference_common//benchmark:request_utils",
        "@inference_common//modules:module_interface",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//utils:file_util",
    ],
)

cc_binary(
    name = "sandbox_benchmark",
    srcs = ["@inference_common//benchmark:sandbox_benchmark.cc"],
    data = [
        "//:inference_sidecar_test_target",
        "//:test_model_target",
    ],
    deps = [
        "//:tensorflow",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_benchmark//:benchmark",
        "@com_google_benchmark//:benchmark_main",
        "@inference_common//benchmark:request_utils",
        "@inference_common//modules:module_interface",
        "@inference_common//proto:inference_sidecar_cc_grpc_proto",
        "@inference_common//proto:inference_sidecar_cc_proto",
        "@inference_common//sandbox:sandbox_executor",
        "@inference_common//utils:file_util",
    ],
)
