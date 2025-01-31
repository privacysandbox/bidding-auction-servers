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

load("@rules_cc//cc:defs.bzl", "cc_library")
load(
    "@rules_pkg//pkg:mappings.bzl",
    "pkg_attributes",
    "pkg_files",
)

package(default_visibility = ["//visibility:public"])

pkg_files(
    name = "roots_pem",
    srcs = ["@com_github_grpc_grpc//:etc/roots.pem"],
    attributes = pkg_attributes(mode = "0555"),
    prefix = "/etc/",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "feature_flags",
    srcs = [
        "feature_flags.cc",
    ],
    hdrs = [
        "feature_flags.h",
    ],
    deps = [
        "@com_google_absl//absl/flags:flag",
    ],
)

cc_library(
    name = "public_key_url_allowlist",
    hdrs = [
        "public_key_url_allowlist.h",
    ],
    deps = [
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)
