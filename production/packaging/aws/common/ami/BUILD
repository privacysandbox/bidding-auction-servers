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

load("@rules_pkg//pkg:zip.bzl", "pkg_zip")

filegroup(
    name = "ami_config_group",
    srcs = select({
        "//conditions:default": [
            "envoy.yaml",
            "envoy_networking.sh",
            "hc.bash",
            "health.proto",
            "otel_collector_config.yaml",
        ],
    }),
    visibility = ["//production/packaging:__subpackages__"],
)

# ami artifacts (NOT in the nitro enclave) for the AMI
pkg_zip(
    name = "nonenclave_artifacts",
    srcs = [
        ":ami_config_group",
        "@google_privacysandbox_servers_common//src/aws/proxy",
    ],
)

genrule(
    name = "copy_to_dist",
    srcs = [
        ":nonenclave_artifacts",
    ],
    outs = ["copy_to_dist.bin"],
    cmd_bash = """cat << EOF > '$@'
mkdir -p dist/aws
cp $(location :nonenclave_artifacts) dist/aws
EOF""",
    executable = True,
    local = True,
    message = "copying AMI artifacts to dist/aws directory",
)

pkg_zip(
    name = "artifacts_proto",
    srcs = [
        "//api:bidding_auction_servers_descriptor_set",
    ],
)

genrule(
    name = "copy_to_dist_proto",
    srcs = [
        ":artifacts_proto",
    ],
    outs = ["copy_to_dist_proto.bin"],
    cmd_bash = """cat << EOF > '$@'
mkdir -p dist/aws
cp $(location :artifacts_proto) dist/aws
EOF""",
    executable = True,
    local = True,
    message = "copying AMI proto to dist/aws directory",
)
