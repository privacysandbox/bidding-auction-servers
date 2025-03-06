# Copyright 2025 Google LLC
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
    name = "v2_adapter_benchmarks",
    testonly = True,
    srcs = [
        "v2_adapter_benchmarks.cc",
    ],
    linkopts = [
        "-Wl,-rpath,\\$$ORIGIN/../lib",
    ],
    deps = [
        "//services/common/clients/kv_server:kv_v2",
        "//services/common/test/utils:proto_utils",
        "//services/common/util:file_util",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:initialize",
        "@com_google_benchmark//:benchmark",
    ],
)
