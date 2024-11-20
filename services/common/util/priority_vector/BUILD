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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "priority_vector_utils",
    srcs = [
        "priority_vector_utils.cc",
    ],
    hdrs = [
        "priority_vector_utils.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/loggers:request_log_context",
        "//services/common/util:json_util",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status:statusor",
        "@rapidjson",
    ],
)

cc_test(
    name = "priority_vector_utils_test",
    srcs = [
        "priority_vector_utils_test.cc",
    ],
    deps = [
        ":priority_vector_utils",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
