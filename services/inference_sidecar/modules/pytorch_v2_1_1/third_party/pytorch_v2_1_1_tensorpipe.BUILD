# Copyright 2024 Google LLC
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

# Copied and modified from https://github.com/pytorch/pytorch/blob/
# 4c55dc50355d5e923642c59ad2a23d6ad54711e7/third_party/tensorpipe.BUILD

load("@pytorch//third_party:substitution.bzl", "header_template_rule")
load("@rules_cc//cc:defs.bzl", "cc_library")

header_template_rule(
    name = "tensorpipe_cpu_config_header",
    src = "tensorpipe/config.h.in",
    out = "tensorpipe/config.h",
    substitutions = {
        "#cmakedefine01 TENSORPIPE_HAS_CMA_CHANNEL": "#define TENSORPIPE_HAS_CMA_CHANNEL 1",
        "#cmakedefine01 TENSORPIPE_HAS_IBV_TRANSPORT": "#define TENSORPIPE_HAS_IBV_TRANSPORT 1",
        "#cmakedefine01 TENSORPIPE_HAS_SHM_TRANSPORT": "#define TENSORPIPE_HAS_SHM_TRANSPORT 1",
    },
)

header_template_rule(
    name = "tensorpipe_cuda_config_header",
    src = "tensorpipe/config_cuda.h.in",
    out = "tensorpipe/config_cuda.h",
    substitutions = {
        "#cmakedefine01 TENSORPIPE_HAS_CUDA_GDR_CHANNEL": "#define TENSORPIPE_HAS_CUDA_GDR_CHANNEL 1",
        "#cmakedefine01 TENSORPIPE_HAS_CUDA_IPC_CHANNEL": "#define TENSORPIPE_HAS_CUDA_IPC_CHANNEL 1",
    },
)

# We explicitly list the CUDA headers & sources, and we consider everything else
# as CPU (using a catch-all glob). This is both because there's fewer CUDA files
# (thus making it easier to list them exhaustively) and because it will make it
# more likely to catch a misclassified file: if we forget to mark a file as CUDA
# we'll try to build it on CPU and that's likely to fail.

TENSORPIPE_CUDA_HEADERS = [
    "tensorpipe/tensorpipe_cuda.h",
    "tensorpipe/channel/cuda_basic/*.h",
    "tensorpipe/channel/cuda_gdr/*.h",
    "tensorpipe/channel/cuda_ipc/*.h",
    "tensorpipe/channel/cuda_xth/*.h",
    "tensorpipe/common/cuda.h",
    "tensorpipe/common/cuda_buffer.h",
    "tensorpipe/common/cuda_lib.h",
    "tensorpipe/common/cuda_loop.h",
    "tensorpipe/common/nvml_lib.h",
]

TENSORPIPE_CUDA_SOURCES = [
    "tensorpipe/channel/cuda_basic/*.cc",
    "tensorpipe/channel/cuda_gdr/*.cc",
    "tensorpipe/channel/cuda_ipc/*.cc",
    "tensorpipe/channel/cuda_xth/*.cc",
    "tensorpipe/common/cuda_buffer.cc",
    "tensorpipe/common/cuda_loop.cc",
]

TENSORPIPE_CPU_HEADERS = glob(
    [
        "tensorpipe/*.h",
        "tensorpipe/channel/*.h",
        "tensorpipe/channel/*/*.h",
        "tensorpipe/common/*.h",
        "tensorpipe/core/*.h",
        "tensorpipe/transport/*.h",
        "tensorpipe/transport/*/*.h",
    ],
    exclude = TENSORPIPE_CUDA_HEADERS,
)

TENSORPIPE_CPU_SOURCES = glob(
    [
        "tensorpipe/*.cc",
        "tensorpipe/channel/*.cc",
        "tensorpipe/channel/*/*.cc",
        "tensorpipe/common/*.cc",
        "tensorpipe/core/*.cc",
        "tensorpipe/transport/*.cc",
        "tensorpipe/transport/*/*.cc",
    ],
    exclude = TENSORPIPE_CUDA_SOURCES,
)

cc_library(
    name = "tensorpipe_cpu",
    srcs = TENSORPIPE_CPU_SOURCES,
    hdrs = TENSORPIPE_CPU_HEADERS + [":tensorpipe_cpu_config_header"],
    copts = [
        "-std=c++14",
    ],
    includes = [
        ".",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@libnop",
        "@libuv",
    ],
)

cc_library(
    name = "tensorpipe_cuda",
    srcs = glob(TENSORPIPE_CUDA_SOURCES),
    hdrs = glob(TENSORPIPE_CUDA_HEADERS) + [":tensorpipe_cuda_config_header"],
    copts = [
        "-std=c++14",
    ],
    includes = [
        ".",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":tensorpipe_cpu",
        "@cuda",
    ],
)
