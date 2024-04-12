load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

### register Python toolchain -- note this toolchain defines the path to a specific version of python
load("//builders/bazel:deps.bzl", "python_deps")

http_archive(
    name = "io_bazel_rules_docker",
    sha256 = "b1e80761a8a8243d03ebca8845e9cc1ba6c82ce7c5179ce2b295cd36f7e394bf",
    urls = ["https://github.com/bazelbuild/rules_docker/releases/download/v0.25.0/rules_docker-v0.25.0.tar.gz"],
)

python_deps("//builders/bazel")

http_archive(
    name = "google_privacysandbox_servers_common",
    # 2024-04-01
    sha256 = "e36cc26c917ec4b1066a32777b48ac8728ba13c276cdda2e91c36ad2037d9bcd",
    strip_prefix = "data-plane-shared-libraries-1fbac466b6b88e00a4ca037f7359ee1942ade13e",
    urls = [
        "https://github.com/privacysandbox/data-plane-shared-libraries/archive/1fbac466b6b88e00a4ca037f7359ee1942ade13e.zip",
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
    # commit af184d649be5d9f0a62738db41ed1496de427bcd 2024-03-15
    sha256 = "",
    strip_prefix = "protected-auction-key-value-service-af184d649be5d9f0a62738db41ed1496de427bcd",
    urls = [
        "https://github.com/privacysandbox/protected-auction-key-value-service/archive/af184d649be5d9f0a62738db41ed1496de427bcd.zip",
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

# TODO: Remove this once we sync to the common repo version that includes this
# as a dependecy.
http_archive(
    name = "bazel_clang_tidy",
    patch_args = ["-p1"],
    patches = ["//third_party:bazel_clang_tidy.patch"],
    sha256 = "352aeb57ad7ed53ff6e02344885de426421fb6fd7a3890b04d14768d759eb598",
    strip_prefix = "bazel_clang_tidy-4884c32e09c1ea9ac96b3f08c3004f3ac4c3fe39",
    urls = [
        "https://github.com/erenon/bazel_clang_tidy/archive/4884c32e09c1ea9ac96b3f08c3004f3ac4c3fe39.zip",
    ],
)
