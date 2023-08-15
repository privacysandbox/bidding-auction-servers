load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

### Git Submodules
local_repository(
    name = "google_privacysandbox_functionaltest_system",
    path = "testing/functionaltest-system",
)

### register Python toolchain -- note this toolchain defines the path to a specific version of python
load("//builders/bazel:deps.bzl", "python_deps")

python_deps("//builders/bazel")

http_archive(
    name = "google_privacysandbox_servers_common",
    sha256 = "34d753551cdcf9d5ddd8f95ef9addc367ff9e75ab4bb86d39e71d51c8c3ca9d9",
    strip_prefix = "data-plane-shared-libraries-ef3b155faa4f6ccd363514011c4188298020d41a",
    urls = [
        "https://github.com/privacysandbox/data-plane-shared-libraries/archive/ef3b155faa4f6ccd363514011c4188298020d41a.zip",
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

load("@control_plane_shared//build_defs/shared:rpm.bzl", rpmpack_repositories = "rpm")

rpmpack_repositories()

load("@com_github_google_rpmpack//:deps.bzl", "rpmpack_dependencies")

rpmpack_dependencies()

# Load the googleapis dependency.
http_archive(
    name = "com_google_googleapis",
    build_file = "//third_party:googleapis.BUILD",
    patch_args = ["-p1"],
    # Scaffolding for patching googleapis after download. For example:
    #   patches = ["googleapis.patch"]
    # NOTE: This should only be used while developing with a new
    # protobuf message. No changes to `patches` should ever be
    # committed to the main branch.
    patch_tool = "patch",
    patches = [],
    sha256 = "3e48e5833fcd2e1fcb8b6a5b7a88e18503b670e8636b868cdb5ac32e00fbdafb",
    strip_prefix = "googleapis-2da477b6a72168c65fdb4245530cfa702cc4b029",
    urls = [
        "https://storage.googleapis.com/cloud-cpp-community-archive/com_google_googleapis/2da477b6a72168c65fdb4245530cfa702cc4b029.tar.gz",
        "https://github.com/googleapis/googleapis/archive/2da477b6a72168c65fdb4245530cfa702cc4b029.tar.gz",
    ],
)

# libcbor
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
    name = "com_google_differential_privacy",
    sha256 = "b2e9afb2ea9337bb7c6302545b72e938707e8cdb3558ef38ce5cdd12fe2f182c",
    strip_prefix = "differential-privacy-2.1.0",
    url = "https://github.com/google/differential-privacy/archive/refs/tags/v2.1.0.tar.gz",
)

http_archive(
    name = "com_google_cc_differential_privacy",
    patch_args = ["-p1"],
    patches = ["//third_party:differential_privacy.patch"],
    sha256 = "b2e9afb2ea9337bb7c6302545b72e938707e8cdb3558ef38ce5cdd12fe2f182c",
    strip_prefix = "differential-privacy-2.1.0/cc",
    urls = ["https://github.com/google/differential-privacy/archive/refs/tags/v2.1.0.tar.gz"],
)
