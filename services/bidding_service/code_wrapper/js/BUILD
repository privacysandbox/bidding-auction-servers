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

load("@io_bazel_rules_closure//closure:defs.bzl", "closure_js_binary", "closure_js_library", "closure_js_test")

package(default_visibility = ["//visibility:public"])

closure_js_library(
    name = "private_aggregation_wrapper",
    srcs = ["private_aggregation_wrapper.js"],
    suppress = [
        "checkTypes",
        "reportUnknownTypes",
    ],
    deps = [
        ":bidding_private_aggregation_util",
        ":bidding_private_aggregation_util_externs",
        "//services/common/private_aggregation/js:private_aggregation_externs",
        "//services/common/private_aggregation/js:private_aggregation_helper_functions_externs",
        "//services/common/private_aggregation/js:private_aggregation_helper_functions_lib",
    ],
)

closure_js_library(
    name = "bidding_private_aggregation_util_externs",
    srcs = ["bidding_private_aggregation_util_externs.js"],
)

closure_js_library(
    name = "bidding_private_aggregation_util",
    srcs = ["bidding_private_aggregation_util.js"],
    deps = [":bidding_private_aggregation_util_externs"],
)

closure_js_binary(
    name = "private_aggregation_wrapper_bin",
    compilation_level = "SIMPLE",
    debug = False,
    formatting = "PRETTY_PRINT",
    language = "ECMASCRIPT_2021",
    visibility = ["//visibility:public"],
    deps = [
        ":bidding_private_aggregation_util",
        ":private_aggregation_wrapper",
        "//services/common/private_aggregation/js:private_aggregation_externs",
        "//services/common/private_aggregation/js:private_aggregation_helper_functions_lib",
    ],
)

closure_js_test(
    name = "bidding_private_aggregation_util_test",
    srcs = ["bidding_private_aggregation_util_test.js"],
    entry_points = [
        "bidding_service.privateAggregationUtil.test",
    ],
    deps = [
        ":bidding_private_aggregation_util",
        "@com_google_javascript_closure_library//closure/goog/testing:testsuite",
    ],
)

closure_js_test(
    name = "private_aggregation_wrapper_test",
    srcs = ["private_aggregation_wrapper_test.js"],
    entry_points = [
        "bidding_service.privateAggregation.test",
    ],
    suppress = ["reportUnknownTypes"],
    deps = [
        ":bidding_private_aggregation_util",
        ":private_aggregation_wrapper",
        "//services/common/private_aggregation/js:private_aggregation_helper_functions_lib",
        "@com_google_javascript_closure_library//closure/goog/testing:asserts",
        "@com_google_javascript_closure_library//closure/goog/testing:jsunit",
        "@com_google_javascript_closure_library//closure/goog/testing:testsuite",
    ],
)
