# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Copied and modified from https://github.com/tensorflow/runtime/blob/b1c7cce21ba4661c17ac72421c6a0e2015e7bef3/third_party/rules_cuda/cuda/dependencies.bzl

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def _local_cuda_impl(repository_ctx):
    # cuda has not been enabled for the build, and this repository is not local
    # cuda lib at /usr/local/cuda.
    defs_template = "def if_local_cuda(true, false = []):\n    return %s"
    repository_ctx.file("BUILD")  # Empty file
    repository_ctx.file("defs.bzl", defs_template % "false")

_local_cuda = repository_rule(
    implementation = _local_cuda_impl,
    environ = ["CUDA_PATH", "PATH"],
    # remotable = True,
)

def pytorch_v2_1_1_deps2():
    maybe(
        name = "pytorch_v2_1_1_bazel_skylib",
        repo_rule = http_archive,
        sha256 = "97e70364e9249702246c0e9444bccdc4b847bed1eb03c5a3ece4f83dfe6abc44",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
        ],
    )
    maybe(
        name = "pytorch_v2_1_1_platforms",
        repo_rule = http_archive,
        sha256 = "48a2d8d343863989c232843e01afc8a986eb8738766bfd8611420a7db8f6f0c3",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/platforms/releases/download/0.0.2/platforms-0.0.2.tar.gz",
            "https://github.com/bazelbuild/platforms/releases/download/0.0.2/platforms-0.0.2.tar.gz",
        ],
    )
    _local_cuda(name = "pytorch_v2_1_1_local_cuda")
    maybe(
        name = "pytorch_v2_1_1_rules_cuda",
        repo_rule = http_archive,
        strip_prefix = "runtime-b1c7cce21ba4661c17ac72421c6a0e2015e7bef3/third_party/rules_cuda",
        sha256 = "f80438bee9906e9ecb1a8a4ae2365374ac1e8a283897281a2db2fb7fcf746333",
        urls = ["https://github.com/tensorflow/runtime/archive/b1c7cce21ba4661c17ac72421c6a0e2015e7bef3.tar.gz"],
        repo_mapping = {
            "@bazel_skylib": "@pytorch_v2_1_1_bazel_skylib",
            "@local_cuda": "@pytorch_v2_1_1_local_cuda",
            "@platforms": "@pytorch_v2_1_1_platforms",
            "@rules_cuda": "@pytorch_v2_1_1_rules_cuda",
        },
    )
