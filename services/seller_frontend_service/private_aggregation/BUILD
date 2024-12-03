# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "private_aggregation_helper",
    srcs = [
        "private_aggregation_helper.cc",
    ],
    hdrs = [
        "private_aggregation_helper.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/loggers:request_log_context",
        "//services/common/private_aggregation:private_aggregation_post_auction_util",
        "//services/common/util:reporting_util",
        "//services/seller_frontend_service/data:seller_frontend_data",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
    ],
)

cc_test(
    name = "private_aggregation_helper_test",
    size = "small",
    srcs = [
        "private_aggregation_helper_test.cc",
    ],
    deps = [
        ":private_aggregation_helper",
        "//services/common/private_aggregation:private_aggregation_test_util",
        "//services/common/test/utils:test_init",
        "//services/seller_frontend_service",
        "//services/seller_frontend_service/data:seller_frontend_data",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
