# Copyright 2025 Google LLC
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
    name = "rng",
    srcs = [
        "rng.cc",
    ],
    hdrs = [
        "rng.h",
    ],
    deps = [
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/random:bit_gen_ref",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_library(
    name = "mock_rng",
    hdrs = ["mock_rng.h"],
    deps = [
        ":rng",
        "@com_google_googletest//:gtest",
    ],
)
