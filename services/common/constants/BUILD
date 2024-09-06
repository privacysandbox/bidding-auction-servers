# Copyright 2022 Google LLC
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

load("@rules_cc//cc:defs.bzl", "cc_library")

package(
    default_visibility = [
        "//services:__subpackages__",
    ],
    licenses = ["notice"],
)

cc_library(
    name = "common_service_flags",
    srcs = [
        "common_service_flags.cc",
    ],
    hdrs = [
        "common_service_flags.h",
    ],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@google_privacysandbox_servers_common//src/telemetry/flag:telemetry_flag",
    ],
)

cc_library(
    name = "user_error_strings",
    hdrs = [
        "user_error_strings.h",
    ],
)

cc_library(
    name = "common_constants",
    hdrs = [
        "common_constants.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)
