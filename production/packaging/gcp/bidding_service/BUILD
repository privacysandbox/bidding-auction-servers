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

load("@bazel_skylib//rules:copy_file.bzl", "copy_file")
load("@google_privacysandbox_servers_common//src/roma/byob/config:container.bzl", "roma_container_dir", "roma_container_root_dir")
load("@google_privacysandbox_servers_common//third_party:container_deps.bzl", "get_user")
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
)
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("@rules_pkg//pkg:zip.bzl", "pkg_zip")

user = get_user("root")

pkg_files(
    name = "server_executables",
    srcs = [
        "bin/init_server_basic",
        "//services/bidding_service:server",
        "@com_github_grpc_grpc//test/cpp/util:grpc_cli",
    ] + select({
        "//:include_all_inference_binaries": [
            "@pytorch_v2_1_1//:artifacts/inference_sidecar_pytorch_v2_1_1",
            "@tensorflow_v2_14_0//:artifacts/inference_sidecar_tensorflow_v2_14_0",
        ],
        "//:inference_noop": [
            "@inference_common//:inference_sidecar_test_target",
        ],
        "//:inference_pytorch": [
            "@pytorch_v2_1_1//:artifacts/inference_sidecar_pytorch_v2_1_1",
        ],
        "//:inference_tensorflow": [
            "@tensorflow_v2_14_0//:artifacts/inference_sidecar_tensorflow_v2_14_0",
        ],
        "//conditions:default": [],
    }),
    attributes = pkg_attributes(mode = "0555"),
    prefix = "/server/bin",
)

server_binaries = [
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

cddl_specs = [
    "//services/bidding_service:packaged_cddl_specs",
]

pkg_zip(
    name = "cddl_specs",
    srcs = cddl_specs,
)

pkg_tar(
    name = "cddl_specs_tar",
    srcs = cddl_specs,
)

container_layer(
    name = "cddl_specs_layer",
    directory = "/",
    tars = [
        ":cddl_specs_tar",
    ],
)

copy_file(
    name = "libcddl_so_lib",
    src = "@cddl_lib//:cddl",
    out = "libcddl.so",
    is_executable = False,
    visibility = ["//visibility:private"],
)

pkg_zip(
    name = "cddl_lib_artifacts",
    srcs = [":libcddl_so_lib"],
)

pkg_tar(
    name = "cddl_lib_artifacts_tar",
    srcs = [":libcddl_so_lib"],
)

container_layer(
    name = "cddl_lib_layer",
    directory = "/lib",
    tars = [
        ":cddl_lib_artifacts_tar",
    ],
)

# gVisor dependencies
pkg_files(
    name = "gvisor_config_file",
    srcs = ["@google_privacysandbox_servers_common//src/roma/byob/container:container_config"],
    attributes = pkg_attributes(mode = "0600"),
)

pkg_tar(
    name = "gvisor_config_tar",
    srcs = [":gvisor_config_file"],
    owner = "{}.{}".format(
        user.uid,
        user.gid,
    ),
)

container_layer(
    name = "gvisor_config_layer",
    directory = "{}".format(roma_container_dir),
    tars = [
        ":gvisor_config_tar",
    ],
)

container_layer(
    name = "byob_server_container_layer",
    directory = "{roma_container_dir}/{root_dir}".format(
        roma_container_dir = roma_container_dir,
        root_dir = roma_container_root_dir,
    ),
    tars = [
        "@google_privacysandbox_servers_common//src/roma/byob/container:byob_server_container_{user}.tar".format(user = user.user),
    ],
)

container_image(
    name = "server_docker_image",
    base = select({
        "@platforms//cpu:arm64": "@runtime-cc-debian-arm64//image",
        "@platforms//cpu:x86_64": "@runtime-cc-debian-amd64//image",
    }),
    cmd = [
        "/server/bin/init_server_basic",
    ],
    entrypoint = [
        "sh",
    ],
    labels = {"tee.launch_policy.log_redirect": "always"},
    layers = [
        ":cddl_lib_layer",
        ":cddl_specs_layer",
        ":server_binary_layer",
        ":byob_server_container_layer",
        ":gvisor_config_layer",
    ] + select({
        "//:e2e_build": [
        ],
        "//conditions:default": [],
    }),
    ports = ["50051"],
    tars = [
        "@google_privacysandbox_servers_common//src/aws/proxy:libnsm_and_proxify_tar",
        "@google_privacysandbox_servers_common//src/cpio/client_providers/kms_client_provider/aws:kms_binaries",
        "@google_privacysandbox_servers_common//src/roma/byob/container:gvisor_tar_{user}".format(user = user.user),
        "@google_privacysandbox_servers_common//src/roma/byob/container:var_run_runsc_tar_{user}".format(user = user.user),
        "//tools/udf_samples:sample_udf_execs_tar",
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
