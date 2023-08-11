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

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(
    default_visibility = [
        "//visibility:public",
    ],
    licenses = ["notice"],
)

cc_library(
    name = "secure_invoke_lib",
    testonly = True,
    srcs = ["secure_invoke_lib.cc"],
    hdrs = ["secure_invoke_lib.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/seller_frontend_server:async_client",
        "//tools/secure_invoke/payload_generator:payload_packaging_lib",
        "//tools/secure_invoke/payload_generator:payload_packaging_utils",
        "@com_github_google_quiche//quiche:oblivious_http_unstable_api",
    ],
)

cc_binary(
    name = "invoke",
    testonly = True,
    srcs = [
        "secure_invoke.cc",
    ],
    args = [
        "--op=invoke",
    ],
    deps = [
        ":secure_invoke_lib",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//tools/secure_invoke/payload_generator:payload_packaging_lib",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_binary(
    name = "package_payload",
    testonly = True,
    srcs = [
        "secure_invoke.cc",
    ],
    args = [
        "--op=encrypt",
    ],
    deps = [
        ":secure_invoke_lib",
        "//tools/secure_invoke/payload_generator:payload_packaging_lib",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "secure_invoke_lib_test",
    size = "large",
    srcs = ["secure_invoke_lib_test.cc"],
    tags = [
        "flaky",
    ],
    deps = [
        ":secure_invoke_lib",
        "//services/bidding_service/benchmarking:bidding_benchmarking_logger",
        "//services/buyer_frontend_service",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test/utils:service_utils",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
