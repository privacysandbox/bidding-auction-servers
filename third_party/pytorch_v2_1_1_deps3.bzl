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

# Copied and modified from https://github.com/protocolbuffers/protobuf/blob/d1eca4e4b421cd2997495c4b4e65cea6be4e9b8a/protobuf_deps.bzl

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def pytorch_v2_1_1_deps3():
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_bazel_skylib",
        sha256 = "97e70364e9249702246c0e9444bccdc4b847bed1eb03c5a3ece4f83dfe6abc44",
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.0.2/bazel-skylib-1.0.2.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_zlib",
        build_file = "@pytorch_v2_1_1_com_google_protobuf//:third_party/zlib.BUILD",
        sha256 = "629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff",
        strip_prefix = "zlib-1.2.11",
        urls = ["https://github.com/madler/zlib/archive/v1.2.11.tar.gz"],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_six",
        build_file = "@@pytorch_v2_1_1_com_google_protobuf//:third_party/six.BUILD",
        sha256 = "d16a0141ec1a18405cd4ce8b4613101da75da0e9a7aec5bdd4fa804d0e0eba73",
        urls = ["https://pypi.python.org/packages/source/s/six/six-1.12.0.tar.gz"],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_rules_cc",
        sha256 = "9d48151ea71b3e225adfb6867e6d2c7d0dce46cbdc8710d9a9a628574dfd40a0",
        strip_prefix = "rules_cc-818289e5613731ae410efb54218a4077fb9dbb03",
        urls = ["https://github.com/bazelbuild/rules_cc/archive/818289e5613731ae410efb54218a4077fb9dbb03.tar.gz"],
    )
    maybe(
        http_archive,
        name = "rules_java",
        sha256 = "f5a3e477e579231fca27bf202bb0e8fbe4fc6339d63b38ccb87c2760b533d1c3",
        strip_prefix = "rules_java-981f06c3d2bd10225e85209904090eb7b5fb26bd",
        urls = ["https://github.com/bazelbuild/rules_java/archive/981f06c3d2bd10225e85209904090eb7b5fb26bd.tar.gz"],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_rules_proto",
        sha256 = "602e7161d9195e50246177e7c55b2f39950a9cf7366f74ed5f22fd45750cd208",
        strip_prefix = "rules_proto-97d8af4dc474595af3900dd85cb3a29ad28cc313",
        urls = ["https://github.com/bazelbuild/rules_proto/archive/97d8af4dc474595af3900dd85cb3a29ad28cc313.tar.gz"],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_rules_python",
        sha256 = "e5470e92a18aa51830db99a4d9c492cc613761d5bdb7131c04bd92b9834380f6",
        strip_prefix = "rules_python-4b84ad270387a7c439ebdccfd530e2339601ef27",
        urls = ["https://github.com/bazelbuild/rules_python/archive/4b84ad270387a7c439ebdccfd530e2339601ef27.tar.gz"],
    )
    maybe(
        native.local_repository,
        name = "pytorch_v2_1_1_com_google_protobuf",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/protobuf",
        repo_mapping = {
            "@bazel_skylib": "@pytorch_v2_1_1_bazel_skylib",
            "@com_google_protobuf": "@pytorch_v_2_1_1_com_google_protobuf",
            "@rules_cc": "@pytorch_v2_1_1_rules_cc",
            "@rules_java": "@pytorch_v2_1_1_rules_java",
            "@rules_proto": "@pytorch_v2_1_1_rules_proto",
            "@rules_python": "@pytorch_v2_1_1_rules_python",
            "@six": "@pytorch_v2_1_1_six",
            "@zlib": "@pytorch_v2_1_1_zlib",
        },
    )
