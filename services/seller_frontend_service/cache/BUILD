# Copyright 2025 Google LLC
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

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "cache",
    hdrs = [
        "cache.h",
    ],
    deps = [
        ":doubly_linked_list",
        "//services/common/util:event",
        "//services/common/util:event_base",
        "//services/seller_frontend_service/k_anon:k_anon_utils",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_library(
    name = "doubly_linked_list",
    hdrs = [
        "doubly_linked_list.h",
    ],
    deps = [
        "//services/common/util:event",
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "doubly_linked_list_test",
    size = "small",
    srcs = [
        "doubly_linked_list_test.cc",
    ],
    deps = [
        ":doubly_linked_list",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "cache_test",
    size = "small",
    srcs = [
        "cache_test.cc",
    ],
    deps = [
        ":cache",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
    ],
)

cc_binary(
    name = "cache_benchmarks",
    testonly = True,
    srcs = [
        "cache_benchmarks.cc",
    ],
    deps = [
        "//services/common/test/utils:test_init",
        "//services/common/util:hash_util",
        "//services/seller_frontend_service/cache",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/strings",
        "@google_benchmark//:benchmark",
        "@google_benchmark//:benchmark_main",
    ],
)
