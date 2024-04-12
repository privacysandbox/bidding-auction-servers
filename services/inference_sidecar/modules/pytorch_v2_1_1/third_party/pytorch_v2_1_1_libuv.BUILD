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

load("@rules_cc//cc:defs.bzl", "cc_library")

LIBUV_COMMON_SRCS = [
    "src/fs-poll.c",
    "src/idna.c",
    "src/inet.c",
    "src/random.c",
    "src/strscpy.c",
    "src/threadpool.c",
    "src/timer.c",
    "src/uv-common.c",
    "src/uv-data-getter-setters.c",
    "src/version.c",
]

LIBUV_POSIX_SRCS = [
    "src/unix/async.c",
    "src/unix/core.c",
    "src/unix/dl.c",
    "src/unix/fs.c",
    "src/unix/getaddrinfo.c",
    "src/unix/getnameinfo.c",
    "src/unix/loop.c",
    "src/unix/loop-watcher.c",
    "src/unix/pipe.c",
    "src/unix/poll.c",
    "src/unix/process.c",
    "src/unix/random-devurandom.c",
    "src/unix/signal.c",
    "src/unix/stream.c",
    "src/unix/tcp.c",
    "src/unix/thread.c",
    "src/unix/tty.c",
    "src/unix/udp.c",
]

LIBUV_LINUX_SRCS = LIBUV_POSIX_SRCS + [
    "src/unix/proctitle.c",
    "src/unix/linux-core.c",
    "src/unix/linux-inotify.c",
    "src/unix/linux-syscalls.c",
    "src/unix/procfs-exepath.c",
    "src/unix/random-getrandom.c",
    "src/unix/random-sysctl-linux.c",
]

cc_library(
    name = "libuv",
    srcs = LIBUV_COMMON_SRCS + LIBUV_LINUX_SRCS,
    hdrs = glob(
        [
            "include/*.h",
            "include/uv/*.h",
            "src/*.h",
            "src/unix/*.h",
        ],
    ),
    includes = [
        "include",
        "src",
    ],
    visibility = ["//visibility:public"],
)
