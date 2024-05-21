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
    name = "buyer_code_wrapper",
    srcs = ["buyer_code_wrapper.cc"],
    hdrs = [
        "buyer_code_wrapper.h",
    ],
    deps = [
        "//services/bidding_service:bidding_constants",
        "//services/bidding_service:runtime_flags",
        "//services/common/clients/config:config_client",
        "//services/common/constants:common_service_flags",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
    ],
)

cc_library(
    name = "buyer_code_wrapper_test_constants",
    srcs = ["buyer_code_wrapper.cc"],
    hdrs = [
        "buyer_code_wrapper_test_constants.h",
    ],
    deps = [
        ":buyer_code_wrapper",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "buyer_code_wrapper_test",
    size = "small",
    srcs = [
        "buyer_code_wrapper_test.cc",
        "buyer_code_wrapper_test_constants.h",
    ],
    deps = [
        ":buyer_code_wrapper",
        "//services/bidding_service:runtime_flags",
        "//services/common/clients/config:config_client",
        "//services/common/constants:common_service_flags",
        "//services/common/test:mocks",
        "//services/common/util:json_util",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@rapidjson",
    ],
)
