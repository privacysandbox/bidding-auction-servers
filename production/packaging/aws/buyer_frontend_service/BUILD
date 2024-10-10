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

load(
    "@io_bazel_rules_docker//container:container.bzl",
    "container_image",
    "container_layer",
)
load("@io_bazel_rules_docker//contrib:test.bzl", "container_test")
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
    prefix = "/server/bin/",
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

container_layer(
    name = "server_binary_layer",
    directory = "/",
    env = {
        "GLOG_logtostderr": "1",
        "GLOG_stderrthreshold": "0",
        "GRPC_DNS_RESOLVER": "native",
    },
    tars = [
        ":server_binaries_tar",
    ],
)

# This image target is meant for testing running the server in an enclave using
# aws proxy to abstract vsock communication.
container_image(
    name = "server_docker_image",
    base = select({
        "@platforms//cpu:arm64": "@runtime-cc-debian-arm64//image",
        "@platforms//cpu:x86_64": "@runtime-cc-debian-amd64//image",
    }),
    cmd = [
        "/proxify",
        "--",
        "/server/bin/server",
        "--init_config_client=true",
    ],
    entrypoint = [
        "/busybox/sh",
    ],
    layers = [
        ":server_binary_layer",
    ] + select({
        "//:e2e_build": [
        ],
        "//conditions:default": [],
    }),
    tars = [
        "@google_privacysandbox_servers_common//src/aws/proxy:libnsm_and_proxify_tar",
        "@google_privacysandbox_servers_common//src/cpio/client_providers/kms_client_provider/aws:kms_binaries",
    ],
)

container_test(
    name = "structure_test",
    size = "large",
    configs = ["test/structure.yaml"],
    driver = "tar",
    image = ":server_docker_image",
)

container_test(
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
        ":server_docker_image.tar",
        "//api:bidding_auction_servers_descriptor_set",
    ],
    outs = ["copy_to_dist.bin"],
    cmd_bash = """cat << EOF > '$@'
mkdir -p dist/debian
cp $(execpath :server_artifacts) dist/debian/$$(basename $(RULEDIR))_artifacts.zip
cp $(execpath :server_docker_image.tar) dist/debian/$$(basename $(RULEDIR))_image.tar
cp $(execpath //api:bidding_auction_servers_descriptor_set) dist
builders/tools/normalize-dist
EOF""",
    executable = True,
    local = True,
    message = "copying server artifacts to dist/debian directory",
)
