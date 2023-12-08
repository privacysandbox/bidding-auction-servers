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

# Copied and modified from https://github.com/pytorch/pytorch/blob/4c55dc50355d5e923642c59ad2a23d6ad54711e7/WORKSPACE

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

def pytorch_v2_1_1_deps1():
    maybe(
        http_archive,
        name = "pytorch_v2_1_1",
        sha256 = "9b73a7d3e1a2186d80de20b12eec319e740d9ec9ee59e7e1b6b04f34fa961226",
        strip_prefix = "pytorch-4c55dc50355d5e923642c59ad2a23d6ad54711e7",
        patch_args = ["-p1"],
        patches = ["//services/inference_sidecar/modules/pytorch_2.1.1:pytorch_v2_1_1.patch"],
        urls = ["https://github.com/pytorch/pytorch/archive/4c55dc50355d5e923642c59ad2a23d6ad54711e7.tar.gz"],
        repo_mapping = {
            "@asmjit": "@pytorch_v2_1_1_asmjit",
            "@com_github_glog": "@pytorch_v2_1_1_glog",
            "@com_github_google_flatbuffers": "@pytorch_v2_1_1_flatbuffers",
            "@com_google_protobuf": "@pytorch_v2_1_1_com_google_protobuf",
            "@eigen": "@pytorch_v2_1_1_eigen",
            "@fbgemm": "@pytorch_v2_1_1_fbgemm",
            "@fmt": "@pytorch_v2_1_1_fmt",
            "@foxi": "@pytorch_v2_1_1_foxi",
            "@gloo": "@pytorch_v2_1_1_gloo",
            "@ideep": "@pytorch_v2_1_1_ideep",
            "@kineto": "@pytorch_v2_1_1_kineto",
            "@mkl": "@pytorch_v2_1_1_mkl",
            "@mkl_dnn": "@pytorch_v2_1_1_mkl_dnn",
            "@mkl_headers": "@pytorch_v2_1_1_mkl_headers",
            "@onnx": "@pytorch_v2_1_1_onnx",
            "@org_pytorch_cpuinfo": "@pytorch_v2_1_1_cpuinfo",
            "@pip_deps": "@pytorch_v2_1_1_pip_deps",
            "@pytorch": "@pytorch_v2_1_1",
            "@rules_cuda": "@pytorch_v2_1_1_rules_cuda",
            "@sleef": "@pytorch_v2_1_1_sleef",
            "@tbb": "@pytorch_v2_1_1_tbb",
            "@tensorpipe": "@pytorch_v2_1_1_tensorpipe",
        },
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_tbb",
        sha256 = "be111cf161b587812fa3b106fe550efb6f129b8b0b702fef32fac23af9580e5e",
        strip_prefix = "oneTBB-a51a90bc609bb73db8ea13841b5cf7aa4344d4a9",
        build_file = "@pytorch_v2_1_1//:third_party/tbb.BUILD",
        patch_args = ["-p1"],
        patches = ["@pytorch_v2_1_1//:third_party/tbb.patch"],
        urls = ["https://github.com/oneapi-src/oneTBB/archive/a51a90bc609bb73db8ea13841b5cf7aa4344d4a9.tar.gz"],
        repo_mapping = {"@pytorch": "@pytorch_v2_1_1"},
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_mkl_headers",
        sha256 = "2af3494a4bebe5ddccfdc43bacc80fcd78d14c1954b81d2c8e3d73b55527af90",
        build_file = "@pytorch_v2_1_1//:third_party/mkl_headers.BUILD",
        urls = [
            "https://anaconda.org/anaconda/mkl-include/2020.0/download/linux-64/mkl-include-2020.0-166.tar.bz2",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_mkl",
        sha256 = "59154b30dd74561e90d547f9a3af26c75b6f4546210888f09c9d4db8f4bf9d4c",
        build_file = "@pytorch_v2_1_1//:third_party/mkl.BUILD",
        strip_prefix = "lib",
        urls = [
            "https://anaconda.org/anaconda/mkl/2020.0/download/linux-64/mkl-2020.0-166.tar.bz2",
        ],
        repo_mapping = {
            "@mkl_headers": "@pytorch_v2_1_1_mkl_headers",
            "@pytorch": "@pytorch_v2_1_1",
        },
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_glog",
        build_file_content = """
licenses(['notice'])

load(':bazel/glog.bzl', 'glog_library')
glog_library(with_gflags=0)
        """,
        strip_prefix = "glog-0.4.0",
        sha256 = "f28359aeba12f30d73d9e4711ef356dc842886968112162bc73002645139c39c",
        urls = [
            "https://github.com/google/glog/archive/v0.4.0.tar.gz",
        ],
    )
    maybe(
        native.local_repository,
        name = "pytorch_v2_1_1_fbgemm",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/fbgemm",
        repo_mapping = {
            "@asmjit": "@pytorch_v2_1_1_asmjit",
            "@cpuinfo": "@pytorch_v2_1_1_cpuinfo",
        },
    )
    maybe(
        native.local_repository,
        name = "pytorch_v2_1_1_flatbuffers",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/flatbuffers",
    )
    maybe(
        native.local_repository,
        name = "pytorch_v2_1_1_cpuinfo",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/cpuinfo",
        repo_mapping = {"@org_pytorch_cpuinfo": "@pytorch_v2_1_1_cpuinfo"},
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_sleef",
        build_file = "@pytorch_v2_1_1//:third_party/sleef.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/sleef",
        repo_mapping = {"@pytorch": "@pytorch_v2_1_1"},
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_asmjit",
        build_file = "@pytorch_v2_1_1_fbgemm//:third_party/asmjit.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/fbgemm/third_party/asmjit",
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_mkl_dnn",
        build_file = "@pytorch_v2_1_1//:third_party/mkl-dnn.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/ideep/mkl-dnn",
        repo_mapping = {
            "@mkl": "@pytorch_v2_1_1_mkl",
            "@pytorch": "@pytorch_v2_1_1",
            "@tbb": "@pytorch_v2_1_1_tbb",
        },
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_ideep",
        build_file = "@pytorch_v2_1_1//:third_party/ideep.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/ideep",
        repo_mapping = {"@mkl_dnn": "@pytorch_v2_1_1_mkl_dnn"},
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_gloo",
        build_file = "@pytorch_v2_1_1//:third_party/gloo.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/gloo",
        repo_mapping = {"@pytorch": "@pytorch_v2_1_1"},
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_tensorpipe",
        build_file = "@pytorch_v2_1_1//:third_party/tensorpipe.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/tensorpipe",
        repo_mapping = {"@pytorch": "@pytorch_v2_1_1"},
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_eigen",
        build_file = "@pytorch_v2_1_1//:third_party/eigen.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/eigen",
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_foxi",
        build_file = "@pytorch_v2_1_1//:third_party/foxi.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/foxi",
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_kineto",
        build_file = "@pytorch_v2_1_1//:third_party/kineto.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/kineto",
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_fmt",
        build_file = "@pytorch_v2_1_1//:third_party/fmt.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/fmt",
    )
    maybe(
        native.new_local_repository,
        name = "pytorch_v2_1_1_onnx",
        build_file = "@pytorch_v2_1_1//:third_party/onnx.BUILD",
        path = "services/inference_sidecar/modules/pytorch_2.1.1/pytorch_v2_1_1/third_party/onnx",
    )
