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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "buyer_key_value_async_http_client",
    srcs = [
        "buyer_key_value_async_http_client.cc",
    ],
    hdrs = [
        "buyer_key_value_async_http_client.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/clients:async_client",
        "//services/common/clients:client_params_template",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/clients/http_kv_server/util:http_kv_server_gen_url_utils",
        "//services/common/clients/http_kv_server/util:http_kv_server_process_response_utils",
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_metadata",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "fake_buyer_key_value_async_http_client",
    srcs = [
        "fake_buyer_key_value_async_http_client.cc",
    ],
    hdrs = [
        "fake_buyer_key_value_async_http_client.h",
    ],
    deps = [
        ":buyer_key_value_async_http_client",
    ],
)

cc_test(
    name = "fake_buyer_key_value_async_http_client_test",
    srcs = [
        "fake_buyer_key_value_async_http_client_test.cc",
    ],
    data = [
    ],
    deps = [
        ":fake_buyer_key_value_async_http_client",
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "buyer_key_value_async_http_client_test",
    size = "small",
    srcs = [
        "buyer_key_value_async_http_client_test.cc",
    ],
    deps = [
        ":buyer_key_value_async_http_client",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest_main",
    ],
)
