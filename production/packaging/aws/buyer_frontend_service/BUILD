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

load("@container_structure_test//:defs.bzl", "container_structure_test")
load("@rules_oci//oci:defs.bzl", "oci_image", "oci_tarball")
load(
    "@rules_pkg//pkg:mappings.bzl",
    "pkg_attributes",
    "pkg_files",
    "pkg_mklink",
)
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")

pkg_files(
    name = "server_executables",
    srcs = [
        "//services/buyer_frontend_service:server",
    ],
    attributes = pkg_attributes(mode = "0555"),
    prefix = "/",
)

# Create a symlink between where kmstool_enclave_cli expects shell to be
# (/bin/sh) and where it actually is on our image (/busybox/sh).
pkg_mklink(
    name = "busybox_sh_symlink",
    link_name = "/bin/sh",
    target = "/busybox/sh",
)

server_binaries = [
    ":busybox_sh_symlink",
    ":server_executables",
]

pkg_zip(
    name = "server_binaries",
    srcs = server_binaries,
)

pkg_tar(
    name = "server_binaries_tar",
    srcs = server_binaries,
)

# This image target is meant for testing running the server in an enclave using
# aws proxy to abstract vsock communication.
oci_image(
    name = "server_docker_image",
    base = select({
        "@platforms//cpu:arm64": "@runtime-debian-debug-root-arm64//:runtime-debian-debug-root-arm64",
        "@platforms//cpu:x86_64": "@runtime-debian-debug-root-amd64//:runtime-debian-debug-root-amd64",
    }),
    cmd = [
        "/proxify",
        "--",
        "/server",
        "--init_config_client=true",
    ],
    entrypoint = [
        "/busybox/sh",
    ],
    env = {
        "GLOG_logtostderr": "1",
        "GLOG_stderrthreshold": "0",
        "GRPC_DNS_RESOLVER": "native",
    },
    tars = [
        "@google_privacysandbox_servers_common//src/aws/proxy:libnsm_and_proxify_tar",
        "@google_privacysandbox_servers_common//src/cpio/client_providers/kms_client_provider/aws:kms_binaries",
        ":server_binaries_tar",
    ] + select({
        "//:e2e_build": [
        ],
        "//conditions:default": [],
    }),
)

oci_tarball(
    name = "server_docker_tarball",
    image = ":server_docker_image",
    repo_tags = ["bazel/production/packaging/aws/buyer_frontend_service:server_docker_image"],
)

container_structure_test(
    name = "structure_test",
    size = "large",
    configs = ["test/structure.yaml"],
    driver = "tar",
    image = ":server_docker_tarball",
)

container_structure_test(
    name = "commands_test",
    size = "large",
    configs = ["test/commands.yaml"],
    driver = "docker",
    image = ":server_docker_image",
)

# server artifacts
pkg_zip(
    name = "server_artifacts",
    srcs = server_binaries,
)

genrule(
    name = "copy_to_dist",
    srcs = [
        ":server_artifacts",
        ":server_docker_tarball",
        "//api:bidding_auction_servers_descriptor_set",
    ],
    outs = ["copy_to_dist.bin"],
    cmd_bash = """cat << EOF > '$@'
mkdir -p dist/debian
cp $(execpath :server_artifacts) dist/debian/$$(basename $(RULEDIR))_artifacts.zip
cp $(execpath :server_docker_tarball) dist/debian/$$(basename $(RULEDIR))_image.tar
cp $(execpath //api:bidding_auction_servers_descriptor_set) dist
builders/tools/normalize-dist
EOF""",
    executable = True,
    local = True,
    message = "copying server artifacts to dist/debian directory",
)
