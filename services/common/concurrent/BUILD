# Copyright 2022 Google LLC
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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "local_cache",
    hdrs =
        [
            "local_cache.h",
            "static_local_cache.h",
        ],
    linkstatic = True,
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "static_local_cache_test",
    srcs =
        [
            "static_local_cache_test.cc",
        ],
    deps = [
        ":local_cache",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)
