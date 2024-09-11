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
    name = "private_aggregation_post_auction_util",
    srcs = ["private_aggregation_post_auction_util.cc"],
    hdrs = [
        "private_aggregation_post_auction_util.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/util:reporting_util",
        "@com_google_absl//absl/numeric:int128",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@rapidjson",
    ],
)

cc_test(
    name = "private_aggregation_post_auction_util_test",
    size = "small",
    srcs = ["private_aggregation_post_auction_util_test.cc"],
    deps = [
        ":private_aggregation_post_auction_util",
        "//services/common/test:mocks",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "private_aggregation_test_util",
    srcs = ["private_aggregation_test_util.cc"],
    hdrs = [
        "private_aggregation_test_util.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/util:reporting_util",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
    ],
)
