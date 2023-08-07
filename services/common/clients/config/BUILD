# Copyright 2023 Google LLC
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

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "config_client_util",
    srcs = ["trusted_server_config_client_util.cc"],
    hdrs = ["trusted_server_config_client_util.h"],
    deps = [
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@control_plane_shared//cc/public/core/interface:execution_result",
        "@control_plane_shared//cc/public/cpio/interface/instance_client",
    ],
)

cc_library(
    name = "config_client",
    srcs = ["trusted_server_config_client.cc"],
    hdrs = ["trusted_server_config_client.h"],
    deps = [
        "//services/common/telemetry:telemetry_flag",
        "//services/common/util:status_macros",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:str_format",
        "@control_plane_shared//cc/public/core/interface:errors",
        "@control_plane_shared//cc/public/cpio/interface/parameter_client",
    ],
)

cc_test(
    name = "config_client_test",
    size = "small",
    srcs = ["trusted_server_config_client_test.cc"],
    deps = [
        ":config_client",
        "//services/common/test:mocks",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@control_plane_shared//cc/public/cpio/interface:cpio",
        "@control_plane_shared//cc/public/cpio/mock/parameter_client:parameter_client_mock",
    ],
)
