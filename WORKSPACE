load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

### register Python toolchain -- note this toolchain defines the path to a specific version of python
load("//builders/bazel:deps.bzl", "python_deps", "python_register_toolchains")

http_archive(
    name = "io_bazel_rules_docker",
    sha256 = "b1e80761a8a8243d03ebca8845e9cc1ba6c82ce7c5179ce2b295cd36f7e394bf",
    urls = ["https://github.com/bazelbuild/rules_docker/releases/download/v0.25.0/rules_docker-v0.25.0.tar.gz"],
)

python_deps()

python_register_toolchains("//builders/bazel")

http_archive(
    name = "google_privacysandbox_servers_common",
    # 2025-02-11
    sha256 = "96ad94b628d009e229e2fd58d6cb117e29331e1e5bccc19fccf9634eb5943fe6",
    strip_prefix = "data-plane-shared-libraries-b442136d9cbe2872d8d55da95d176fd9a2d77b68",
    urls = [
        "https://github.com/privacysandbox/data-plane-shared-libraries/archive/b442136d9cbe2872d8d55da95d176fd9a2d77b68.zip",
    ],
)

load(
    "@google_privacysandbox_servers_common//third_party:cpp_deps.bzl",
    data_plane_shared_deps_cpp = "cpp_dependencies",
)

data_plane_shared_deps_cpp()

load("@google_privacysandbox_servers_common//third_party:deps1.bzl", data_plane_shared_deps1 = "deps1")

data_plane_shared_deps1()

load("@google_privacysandbox_servers_common//third_party:deps2.bzl", data_plane_shared_deps2 = "deps2")

data_plane_shared_deps2(go_toolchains_version = "1.20.4")

load("@google_privacysandbox_servers_common//third_party:deps3.bzl", data_plane_shared_deps3 = "deps3")

data_plane_shared_deps3()

load("@google_privacysandbox_servers_common//third_party:deps4.bzl", data_plane_shared_deps4 = "deps4")

data_plane_shared_deps4()

load(
    "@io_bazel_rules_docker//repositories:repositories.bzl",
    container_repositories = "repositories",
)

container_repositories()

load("@io_bazel_rules_docker//repositories:deps.bzl", rules_docker_deps = "deps")

rules_docker_deps()

load("//third_party:container_deps.bzl", "container_deps")

container_deps()

load("@com_github_google_rpmpack//:deps.bzl", "rpmpack_dependencies")

rpmpack_dependencies()

http_archive(
    name = "libcbor",
    build_file = "//third_party:libcbor.BUILD",
    patch_args = ["-p1"],
    patches = ["//third_party:libcbor.patch"],
    sha256 = "9fec8ce3071d5c7da8cda397fab5f0a17a60ca6cbaba6503a09a47056a53a4d7",
    strip_prefix = "libcbor-0.10.2/src",
    urls = ["https://github.com/PJK/libcbor/archive/refs/tags/v0.10.2.zip"],
)

http_archive(
    name = "service_value_key_fledge_privacysandbox",
    # commit/db6a0b8593867d1d33fcfa116ee77d893d2b71fa 2025-01-15
    sha256 = "00f42bf01e150a2bbe2e345fa21dc6fc0685a7632cc475937846fa0f643736e5",
    strip_prefix = "protected-auction-key-value-service-1.1.0",
    urls = [
        "https://github.com/privacysandbox/protected-auction-key-value-service/archive/refs/tags/v1.1.0.zip",
    ],
)

### Initialize Python headers

http_archive(
    name = "pybind11_bazel",
    sha256 = "b72c5b44135b90d1ffaba51e08240be0b91707ac60bea08bb4d84b47316211bb",
    strip_prefix = "pybind11_bazel-b162c7c88a253e3f6b673df0c621aca27596ce6b",
    urls = ["https://github.com/pybind/pybind11_bazel/archive/b162c7c88a253e3f6b673df0c621aca27596ce6b.zip"],
)

load("@pybind11_bazel//:python_configure.bzl", "python_configure")

python_configure(
    name = "local_config_python",
)

### Initialize inference common (for bidding server inference utils)
local_repository(
    name = "inference_common",
    path = "services/inference_sidecar/common",
)

### Initialize PyTorch sidecar local repository (for PyTorch sidecar binary)
local_repository(
    name = "pytorch_v2_1_1",
    path = "services/inference_sidecar/modules/pytorch_v2_1_1",
)

### Initialize Tensorflow sidecar local respository

local_repository(
    name = "tensorflow_v2_14_0",
    path = "services/inference_sidecar/modules/tensorflow_v2_14_0",
)

http_archive(
    name = "libevent",
    build_file = "//third_party:libevent.BUILD",
    patch_args = ["-p1"],
    patches = [
        "//third_party:libevent.patch",
    ],
    sha256 = "8836ad722ab211de41cb82fe098911986604f6286f67d10dfb2b6787bf418f49",
    strip_prefix = "libevent-release-2.1.12-stable",
    urls = ["https://github.com/libevent/libevent/archive/refs/tags/release-2.1.12-stable.zip"],
)

load("@rules_rust//crate_universe:repositories.bzl", "crate_universe_dependencies")

crate_universe_dependencies()

load("//third_party:deps.bzl", cddl_deps = "deps")

cddl_deps()

load("@cddl_crate_index//:defs.bzl", cddl_crate_repositories = "crate_repositories")

cddl_crate_repositories()

http_archive(
    name = "cddl_lib",
    build_file = "//third_party/cddl:cddl.BUILD",
    patch_args = ["-p1"],
    patches = [
        "//third_party:cddl/cddl.patch",
    ],
    sha256 = "01e04989c6482e851dc22f376f1c2e1cc493e1ae7b808ae78180d539e6939acb",
    strip_prefix = "cddl-0.9.4",
    urls = ["https://github.com/anweiss/cddl/archive/refs/tags/0.9.4.zip"],
    workspace_file = "//third_party/cddl:WORKSPACE",
)

http_archive(
    name = "com_google_cpp_proto_builder",
    patch_args = ["-p1"],
    patches = [
        "//third_party:cpp_proto_builder/cpp_proto_builder.patch",
    ],
    sha256 = "d36865e2d4e36856b9117b59ef3eab27b92e623418ab9a531c80fee9e1ad521d",
    strip_prefix = "cpp-proto-builder-0.1.0",
    urls = ["https://github.com/google/cpp-proto-builder/archive/refs/tags/v0.1.0.zip"],
)

load("@com_google_cpp_proto_builder//:workspace.bzl", "init_cpp_pb_external_repositories")

init_cpp_pb_external_repositories()

# Needed for libunwind
load("@google_privacysandbox_servers_common//build_defs/cc/shared:sandboxed_api.bzl", "sandboxed_api")

sandboxed_api()

load("@com_google_sandboxed_api//sandboxed_api/bazel:sapi_deps.bzl", "sapi_deps")

sapi_deps()
