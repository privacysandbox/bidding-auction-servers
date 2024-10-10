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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "reporting_response",
    hdrs = ["reporting_response.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_library(
    name = "reporting_helper",
    srcs = ["reporting_helper.cc"],
    hdrs = ["reporting_helper.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":noiser_and_bucketer",
        ":reporting_response",
        "//services/auction_service:auction_constants",
        "//services/common:feature_flags",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/loggers:request_log_context",
        "//services/common/util:json_util",
        "//services/common/util:post_auction_signals",
        "//services/common/util:reporting_util",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
        "@rapidjson",
    ],
)

cc_library(
    name = "reporting_test_util",
    srcs = [
        "reporting_test_util.cc",
    ],
    hdrs = [
        "reporting_helper_test_constants.h",
        "reporting_test_util.cc",
        "reporting_test_util.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":reporting_helper",
        ":reporting_response",
        "//services/auction_service/reporting/seller:seller_reporting_manager",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@rapidjson",
    ],
)

cc_library(
    name = "noiser_and_bucketer",
    srcs = ["noiser_and_bucketer.cc"],
    hdrs = ["noiser_and_bucketer.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@boringssl//:crypto",
        "@boringssl//:ssl",
        "@com_google_absl//absl/random",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "reporting_helper_test",
    size = "small",
    srcs = [
        "reporting_helper_test.cc",
        "reporting_helper_test_constants.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":reporting_helper",
        ":reporting_response",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/util:json_util",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
        "@rapidjson",
    ],
)

cc_test(
    name = "noiser_and_bucketer_test",
    size = "small",
    srcs = [
        "noiser_and_bucketer_test.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":noiser_and_bucketer",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)
