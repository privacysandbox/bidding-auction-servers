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
        patches = ["//:third_party/pytorch_v2_1_1.patch"],
        urls = ["https://github.com/pytorch/pytorch/archive/4c55dc50355d5e923642c59ad2a23d6ad54711e7.tar.gz"],
        repo_mapping = {
            "@asmjit": "@pytorch_v2_1_1_asmjit",
            "@com_github_glog": "@pytorch_v2_1_1_glog",
            "@com_github_google_flatbuffers": "@pytorch_v2_1_1_flatbuffers",
            "@eigen": "@pytorch_v2_1_1_eigen",
            "@fbgemm": "@pytorch_v2_1_1_fbgemm",
            "@fmt": "@pytorch_v2_1_1_fmt",
            "@foxi": "@pytorch_v2_1_1_foxi",
            "@gloo": "@pytorch_v2_1_1_gloo",
            "@kineto": "@pytorch_v2_1_1_kineto",
            "@onnx": "@pytorch_v2_1_1_onnx",
            "@org_pytorch_cpuinfo": "@pytorch_v2_1_1_cpuinfo",
            "@pip_deps": "@pytorch_v2_1_1_pip_deps",
            "@pytorch": "@pytorch_v2_1_1",
            "@rules_cuda": "@pytorch_v2_1_1_rules_cuda",
            "@sleef": "@pytorch_v2_1_1_sleef",
            "@tensorpipe": "@pytorch_v2_1_1_tensorpipe",
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
        http_archive,
        name = "pytorch_v2_1_1_fbgemm",
        strip_prefix = "FBGEMM-cdae5d97e3aa9fda4222f31c04dbd80249c918d1",
        sha256 = "4d180de8471ebea1f89db65572b79461cef1ae714e8dacc9650bcc0a6e1a1ea2",
        urls = [
            "https://github.com/pytorch/fbgemm/archive/cdae5d97e3aa9fda4222f31c04dbd80249c918d1.tar.gz",
        ],
        repo_mapping = {
            "@asmjit": "@pytorch_v2_1_1_asmjit",
            "@cpuinfo": "@pytorch_v2_1_1_cpuinfo",
        },
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_flatbuffers",
        strip_prefix = "flatbuffers-01834de25e4bf3975a9a00e816292b1ad0fe184b",
        sha256 = "5a1f2a116b49a1aaa3563c3d9e3baf6ea256eebf57c7bc144a2d28aa73deba7b",
        urls = [
            "https://github.com/google/flatbuffers/archive/01834de25e4bf3975a9a00e816292b1ad0fe184b.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_cpuinfo",
        strip_prefix = "cpuinfo-6481e8bef08f606ddd627e4d3be89f64d62e1b8a",
        sha256 = "e5c47db587d3ef15537fdd32f929e070aabbcf607e86ab1d86196385dedd2db9",
        urls = [
            "https://github.com/pytorch/cpuinfo/archive/6481e8bef08f606ddd627e4d3be89f64d62e1b8a.tar.gz",
        ],
        repo_mapping = {"@org_pytorch_cpuinfo": "@pytorch_v2_1_1_cpuinfo"},
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_sleef",
        build_file = "@pytorch_v2_1_1//:third_party/sleef.BUILD",
        strip_prefix = "sleef-e0a003ee838b75d11763aa9c3ef17bf71a725bff",
        sha256 = "fdf8901ebeb58924e197dd14d264f7c04e9f80da720f4c38deb13992f1bb89ac",
        urls = [
            "https://github.com/shibatch/sleef/archive/e0a003ee838b75d11763aa9c3ef17bf71a725bff.tar.gz",
        ],
        repo_mapping = {"@pytorch": "@pytorch_v2_1_1"},
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_asmjit",
        build_file = "@pytorch_v2_1_1_fbgemm//:third_party/asmjit.BUILD",
        strip_prefix = "asmjit-d3fbf7c9bc7c1d1365a94a45614b91c5a3706b81",
        sha256 = "bd19e308b30f0c8c8b0d34be356422b4ccce12f9dc590f77ecb886c21bcfedab",
        urls = [
            "https://github.com/asmjit/asmjit/archive/d3fbf7c9bc7c1d1365a94a45614b91c5a3706b81.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_gloo",
        build_file = "@pytorch_v2_1_1//:third_party/gloo.BUILD",
        strip_prefix = "gloo-597accfd79f5b0f9d57b228dec088ca996686475",
        sha256 = "cb734b41f3f5f772312645e44ec5d583e0f4da2b1253e2b3d0ffcba7f6da190a",
        urls = [
            "https://github.com/facebookincubator/gloo/archive/597accfd79f5b0f9d57b228dec088ca996686475.tar.gz",
        ],
        repo_mapping = {"@pytorch": "@pytorch_v2_1_1"},
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_tensorpipe",
        build_file = "//third_party:pytorch_v2_1_1_tensorpipe.BUILD",
        strip_prefix = "tensorpipe-52791a2fd214b2a9dc5759d36725909c1daa7f2e",
        sha256 = "7ff0b84c0623f3360ec7c34b8c4fe02e7f9a87f8fa559c303f9574e44be0bc56",
        urls = [
            "https://github.com/pytorch/tensorpipe/archive/52791a2fd214b2a9dc5759d36725909c1daa7f2e.tar.gz",
        ],
        repo_mapping = {
            "@libnop": "@pytorch_v2_1_1_libnop",
            "@libuv": "@pytorch_v2_1_1_libuv",
            "@pytorch": "@pytorch_v2_1_1",
        },
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_eigen",
        build_file = "@pytorch_v2_1_1//:third_party/eigen.BUILD",
        strip_prefix = "eigen-git-mirror-3147391d946bb4b6c68edd901f2add6ac1f31f8c",
        sha256 = "0974e0d92afc7eb581057a904559155564cbdf65b6c60be13fe4b925b34e3966",
        urls = [
            "https://github.com/eigenteam/eigen-git-mirror/archive/3147391d946bb4b6c68edd901f2add6ac1f31f8c.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_foxi",
        build_file = "@pytorch_v2_1_1//:third_party/foxi.BUILD",
        strip_prefix = "foxi-c278588e34e535f0bb8f00df3880d26928038cad",
        sha256 = "52665aae3c7352a1fa68d017a87c825559c514072409e30753f36aa6eb0c7b4d",
        urls = [
            "https://github.com/houseroad/foxi/archive/c278588e34e535f0bb8f00df3880d26928038cad.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_kineto",
        build_file = "@pytorch_v2_1_1//:third_party/kineto.BUILD",
        strip_prefix = "kineto-49e854d805d916b2031e337763928d2f8d2e1fbf",
        sha256 = "f260688160d043db4ffc2338502a1343b0ecaec0accc6dd5e535ee898eb619a1",
        urls = [
            "https://github.com/pytorch/kineto/archive/49e854d805d916b2031e337763928d2f8d2e1fbf.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_fmt",
        build_file = "@pytorch_v2_1_1//:third_party/fmt.BUILD",
        strip_prefix = "fmt-e57ca2e3685b160617d3d95fcd9e789c4e06ca88",
        sha256 = "2d18d7b1c393791a180c8321cedd702093b5527625be955178bef8af2b1cf6db",
        urls = [
            "https://github.com/fmtlib/fmt/archive/e57ca2e3685b160617d3d95fcd9e789c4e06ca88.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_onnx",
        build_file = "@pytorch_v2_1_1//:third_party/onnx.BUILD",
        strip_prefix = "onnx-1014f41f17ecc778d63e760a994579d96ba471ff",
        sha256 = "0dbfa8eaf48c983aabbca8add6ba9ec7cf12268daecbd081bdbd81dac192b668",
        urls = [
            "https://github.com/onnx/onnx/archive/1014f41f17ecc778d63e760a994579d96ba471ff.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_libuv",
        build_file = "//third_party:pytorch_v2_1_1_libuv.BUILD",
        strip_prefix = "libuv-1dff88e5161cba5c59276d2070d2e304e4dcb242",
        sha256 = "c66c8a024bb31c6134347a0d5630f6275c0bfe94d4144504d45099412fd7a054",
        urls = [
            "https://github.com/libuv/libuv/archive/1dff88e5161cba5c59276d2070d2e304e4dcb242.tar.gz",
        ],
    )
    maybe(
        http_archive,
        name = "pytorch_v2_1_1_libnop",
        build_file = "//third_party:pytorch_v2_1_1_libnop.BUILD",
        strip_prefix = "libnop-910b55815be16109f04f4180e9adee14fb4ce281",
        sha256 = "ec3604671f8ea11aed9588825f9098057ebfef7a8908e97459835150eea9f63a",
        urls = [
            "https://github.com/google/libnop/archive/910b55815be16109f04f4180e9adee14fb4ce281.tar.gz",
        ],
    )
