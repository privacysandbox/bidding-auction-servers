# Copyright 2023 Google LLC
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
    name = "seller_code_wrapper",
    srcs = ["seller_code_wrapper.cc"],
    hdrs = [
        "seller_code_wrapper.h",
    ],
    deps = [
        "//services/common/util:reporting_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "seller_code_wrapper_test",
    size = "small",
    srcs = [
        "seller_code_wrapper_test.cc",
        "seller_code_wrapper_test_constants.h",
    ],
    deps = [
        ":seller_code_wrapper",
        "//services/common/util:json_util",
        "//services/common/util:reporting_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@rapidjson",
    ],
)

cc_library(
    name = "buyer_reporting_fetcher",
    srcs = ["buyer_reporting_fetcher.cc"],
    hdrs = [
        "buyer_reporting_fetcher.h",
    ],
    deps = [
        "//services/auction_service:auction_code_fetch_config_cc_proto",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/code_fetch:code_fetcher_interface",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)
