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

cc_library(
    name = "reporting_response",
    hdrs = ["reporting_response.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/util:status_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "reporting_helper",
    srcs = ["reporting_helper.cc"],
    hdrs = ["reporting_helper.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":reporting_response",
        "//services/common/clients/code_dispatcher:code_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/util:context_logger",
        "//services/common/util:json_util",
        "//services/common/util:post_auction_signals",
        "//services/common/util:reporting_util",
        "//services/common/util:request_response_constants",
        "//services/common/util:status_macros",
        "//services/common/util:status_util",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@rapidjson",
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
        "//services/common/clients/code_dispatcher:code_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/util:json_util",
        "//services/common/util:request_response_constants",
        "//services/common/util:status_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@rapidjson",
    ],
)
