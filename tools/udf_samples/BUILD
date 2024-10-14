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
load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

package(
    default_visibility = ["//visibility:public"],
)

cc_binary(
    name = "sample_generate_bid_udf",
    srcs = ["sample_generate_bid_udf.cc"],
    deps = [
        "//api/udf:generate_bid_sdk_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

filegroup(
    name = "sample_udf_binaries",
    srcs = [
        ":sample_generate_bid_udf",
    ],
)

pkg_files(
    name = "sample_udf_execs",
    srcs = [
        ":sample_udf_binaries",
    ],
    attributes = pkg_attributes(mode = "0555"),
    prefix = "/sample_udf/bin/",
)

pkg_tar(
    name = "sample_udf_execs_tar",
    srcs = [
        ":sample_udf_execs",
    ],
)
