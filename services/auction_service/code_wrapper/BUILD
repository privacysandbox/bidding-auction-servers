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
    name = "buyer_reporting_udf_wrapper",
    srcs = ["buyer_reporting_udf_wrapper.cc"],
    hdrs = [
        "buyer_reporting_udf_wrapper.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "buyer_reporting_udf_wrapper_test",
    size = "small",
    srcs = [
        "buyer_reporting_test_constants.h",
        "buyer_reporting_udf_wrapper_test.cc",
    ],
    deps = [
        ":buyer_reporting_udf_wrapper",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "seller_code_wrapper",
    srcs = ["seller_code_wrapper.cc"],
    hdrs = [
        "seller_code_wrapper.h",
    ],
    deps = [
        ":seller_udf_wrapper",
        "//services/common/util:reporting_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "seller_udf_wrapper",
    srcs = ["seller_udf_wrapper.cc"],
    hdrs = [
        "seller_udf_wrapper.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "seller_code_wrapper_test",
    size = "small",
    srcs = [
        "seller_code_wrapper_test.cc",
        "seller_code_wrapper_test_constants.h",
        "seller_udf_wrapper_test_constants.h",
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

cc_test(
    name = "seller_udf_wrapper_test",
    size = "small",
    srcs = [
        "seller_udf_wrapper_test.cc",
        "seller_udf_wrapper_test_constants.h",
    ],
    deps = [
        ":seller_udf_wrapper",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
