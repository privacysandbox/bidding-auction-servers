load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

### register Python toolchain -- note this toolchain defines the path to a specific version of python
load("//builders/bazel:deps.bzl", "python_deps")

python_deps("//builders/bazel")

http_archive(
    name = "google_privacysandbox_servers_common",
    # 2024-02-15
    sha256 = "4073bbbce12efe4d1f1d3586a86bfa712ddf373605f4626abf1c43ec1772c14b",
    strip_prefix = "data-plane-shared-libraries-a4b61aeca9da3ce79a03a5ee4aa32e0acaa78cb6",
    urls = [
        "https://github.com/privacysandbox/data-plane-shared-libraries/archive/a4b61aeca9da3ce79a03a5ee4aa32e0acaa78cb6.zip",
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
    sha256 = "f9705cfa29e7ba1fb196a30a8dd11fc314077d7f693091d24fa8597612ac160a",
    strip_prefix = "protected-auction-key-value-service-1eee8e79e44f3ca735cfab0b716e57f81d95bd46",
    urls = [
        "https://github.com/privacysandbox/protected-auction-key-value-service/archive/1eee8e79e44f3ca735cfab0b716e57f81d95bd46.zip",
    ],
)
