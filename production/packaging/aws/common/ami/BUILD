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
    srcs = glob(["*.yaml"]),
    visibility = ["//production/packaging:__subpackages__"],
)

# ami artifacts (NOT in the nitro enclave) for the AMI
pkg_zip(
    name = "nonenclave_artifacts",
    srcs = [
        ":ami_config_group",
        "//api:bidding_auction_servers_descriptor_set",
        "@control_plane_shared//cc/aws/proxy/src:proxy",
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