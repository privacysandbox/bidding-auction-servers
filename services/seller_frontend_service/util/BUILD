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

package(default_visibility = ["//services/seller_frontend_service:__pkg__"])

cc_library(
    name = "startup_param_parser",
    srcs = [
        "config_param_parser.cc",
    ],
    hdrs = [
        "config_param_parser.h",
    ],
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@rapidjson",
    ],
)

cc_test(
    name = "startup_param_parser_test",
    srcs = [
        "config_param_parser_test.cc",
    ],
    deps = [
        ":startup_param_parser",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "web_utils",
    srcs = [
        "web_utils.cc",
    ],
    hdrs = [
        "web_utils.h",
    ],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/compression:gzip",
        "//services/common/util:error_accumulator",
        "//services/common/util:request_response_constants",
        "//services/common/util:scoped_cbor",
        "//services/common/util:status_macros",
        "@com_github_google_glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@google_privacysandbox_servers_common//src/cpp/communication:compression",
        "@libcbor//:cbor",
        "@rapidjson",
    ],
)

cc_test(
    name = "web_utils_test",
    srcs = [
        "web_utils_test.cc",
    ],
    deps = [
        ":web_utils",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:cbor_test_utils",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "select_ad_reactor_test_utils",
    testonly = True,
    srcs = ["select_ad_reactor_test_utils.cc"],
    hdrs = ["select_ad_reactor_test_utils.h"],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/compression:gzip",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:cbor_test_utils",
        "//services/common/test/utils:ohttp_test_utils",
        "//services/common/util:context_logger",
        "//services/common/util:error_accumulator",
        "//services/seller_frontend_service",
        "//services/seller_frontend_service:seller_frontend_data",
        "//services/seller_frontend_service/test:app_test_utils",
        "//services/seller_frontend_service/util:framing_utils",
        "@com_github_google_quiche//quiche:oblivious_http_unstable_api",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/cpp/communication:encoding_utils",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_library(
    name = "framing_utils",
    srcs = ["framing_utils.cc"],
    hdrs = ["framing_utils.h"],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/numeric:bits",
    ],
)
