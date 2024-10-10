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

package(default_visibility = ["//services/auction_service:__pkg__"])

cc_library(
    name = "proto_utils",
    srcs = [
        "proto_utils.cc",
    ],
    hdrs = [
        "proto_utils.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/auction_service:auction_constants",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "//services/common/loggers:request_log_context",
        "//services/common/util:json_util",
        "//services/common/util:reporting_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_test(
    name = "proto_utils_test",
    srcs = [
        "proto_utils_test.cc",
    ],
    deps = [
        ":proto_utils",
        "//services/auction_service:auction_constants",
        "//services/auction_service:score_ads_reactor_test_util",
        "//services/common/test:random",
        "//services/common/util:json_util",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/core/test/utils",
    ],
)
