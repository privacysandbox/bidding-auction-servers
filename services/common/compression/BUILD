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

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "compression_utils",
    srcs = ["compression_utils.cc"],
    hdrs = [
        "compression_utils.h",
    ],
    deps = [
        ":gzip",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@zlib",
    ],
)

cc_library(
    name = "gzip",
    srcs = ["gzip.cc"],
    hdrs = [
        "gzip.h",
    ],
    deps = [
        "@com_google_absl//absl/status:statusor",
        "@zlib",
    ],
)

cc_test(
    name = "gzip_test",
    size = "small",
    srcs = [
        "gzip_test.cc",
    ],
    deps = [
        ":gzip",
        "@boost//:iostreams",
        "@com_google_googletest//:gtest_main",
    ],
)
