load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

### register Python toolchain -- note this toolchain defines the path to a specific version of python
load("//builders/bazel:deps.bzl", "python_deps")

python_deps("//builders/bazel")

http_archive(
    name = "google_privacysandbox_servers_common",
    # commit 2023-12-13
    sha256 = "c45a8a7a714b5fc1f4e31a7fe6691eeee5817f91d29f786affc37fbeafc6e13d",
    strip_prefix = "data-plane-shared-libraries-578c988ad077fa46056335fd07d316c26f452285",
    urls = [
        "https://github.com/privacysandbox/data-plane-shared-libraries/archive/578c988ad077fa46056335fd07d316c26f452285.zip",
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
    # commit 1eee8e79e44f3ca735cfab0b716e57f81d95bd46 2023-10-26
    strip_prefix = "fledge-key-value-service-1eee8e79e44f3ca735cfab0b716e57f81d95bd46",
    urls = [
        "https://github.com/privacysandbox/fledge-key-value-service/archive/1eee8e79e44f3ca735cfab0b716e57f81d95bd46.zip",
    ],
)
