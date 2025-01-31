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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "private_aggregation_manager",
    srcs = [
        "private_aggregation_manager.cc",
    ],
    hdrs = [
        "private_aggregation_manager.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/loggers:request_log_context",
        "//services/common/private_aggregation:private_aggregation_post_auction_util",
        "//services/common/util:json_util",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "private_aggregation_manager_test",
    timeout = "short",
    srcs = [
        "private_aggregation_manager_test.cc",
    ],
    deps = [
        ":private_aggregation_manager_test_util",
        "//services/common/private_aggregation:private_aggregation_test_util",
    ],
)

cc_library(
    name = "private_aggregation_manager_test_util",
    testonly = True,
    srcs = ["private_aggregation_manager_test_util.cc"],
    hdrs = ["private_aggregation_manager_test_util.h"],
    deps = [
        ":private_aggregation_manager",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)
