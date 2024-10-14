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

cc_library(
    name = "pas_buyer_reporting_manager",
    srcs = [
        "pas_buyer_reporting_manager.cc",
    ],
    hdrs = [
        "pas_buyer_reporting_manager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":buyer_reporting_helper",
        "//services/auction_service/code_wrapper:buyer_reporting_udf_wrapper",
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_response",
        "//services/auction_service/udf_fetcher:adtech_code_version_util",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "@com_google_absl//absl/status:statusor",
        "@rapidjson",
    ],
)

cc_library(
    name = "pa_buyer_reporting_manager",
    srcs = [
        "pa_buyer_reporting_manager.cc",
    ],
    hdrs = [
        "pa_buyer_reporting_manager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":buyer_reporting_helper",
        "//services/auction_service/code_wrapper:buyer_reporting_udf_wrapper",
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_response",
        "//services/auction_service/udf_fetcher:adtech_code_version_util",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "@com_google_absl//absl/status:statusor",
        "@rapidjson",
    ],
)

cc_library(
    name = "buyer_reporting_helper",
    srcs = [
        "buyer_reporting_helper.cc",
    ],
    hdrs = ["buyer_reporting_helper.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//services/auction_service/reporting:noiser_and_bucketer",
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_response",
        "//services/common/util:json_util",
        "@com_google_absl//absl/status:statusor",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_test(
    name = "pas_buyer_reporting_manager_test",
    size = "small",
    srcs = [
        "pas_buyer_reporting_manager_test.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":pas_buyer_reporting_manager",
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_helper_test",
        "//services/auction_service/reporting:reporting_response",
        "//services/auction_service/reporting:reporting_test_util",
        "//services/auction_service/udf_fetcher:adtech_code_version_util",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@rapidjson",
    ],
)

cc_test(
    name = "pa_buyer_reporting_manager_test",
    size = "small",
    srcs = [
        "pa_buyer_reporting_manager_test.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":pa_buyer_reporting_manager",
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_helper_test",
        "//services/auction_service/reporting:reporting_response",
        "//services/auction_service/reporting:reporting_test_util",
        "//services/auction_service/udf_fetcher:adtech_code_version_util",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@rapidjson",
    ],
)

cc_test(
    name = "buyer_reporting_helper_test",
    size = "small",
    srcs = [
        "buyer_reporting_helper_test.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":buyer_reporting_helper",
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_helper_test",
        "//services/auction_service/reporting:reporting_response",
        "//services/auction_service/reporting:reporting_test_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@rapidjson",
    ],
)
