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

package(
    default_visibility = [
        "//visibility:public",
    ],
    licenses = ["notice"],
)

cc_library(
    name = "payload_packaging_utils",
    testonly = True,
    srcs = ["payload_packaging_utils.cc"],
    hdrs = ["payload_packaging_utils.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/test/utils:cbor_test_utils",
        "//services/common/test/utils:ohttp_test_utils",
        "//services/common/util:hpke_utils",
        "//services/common/util:oblivious_http_utils",
        "//services/seller_frontend_service/util:framing_utils",
        "//services/seller_frontend_service/util:proto_mapping_util",
        "//services/seller_frontend_service/util:select_ad_reactor_test_utils",
        "@com_github_google_quiche//quiche:oblivious_http_unstable_api",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:fake_key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "payload_packaging_utils_test",
    size = "small",
    srcs = [
        "payload_packaging_utils_test.cc",
    ],
    deps = [
        ":payload_packaging_utils",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/test:random",
        "//services/common/util:hpke_utils",
        "//services/common/util:request_response_constants",
        "//services/seller_frontend_service/util:framing_utils",
        "//services/seller_frontend_service/util:proto_mapping_util",
        "//services/seller_frontend_service/util:select_ad_reactor_test_utils",
        "//services/seller_frontend_service/util:web_utils",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/communication:encoding_utils",
        "@google_privacysandbox_servers_common//src/communication:ohttp_utils",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/interface:key_fetcher_manager_interface",
    ],
)

cc_library(
    name = "payload_packaging_lib",
    testonly = True,
    srcs = [
        "payload_packaging.cc",
    ],
    hdrs = [
        "payload_packaging.h",
    ],
    deps = [
        ":payload_packaging_utils",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/util:json_util",
        "@com_github_google_quiche//quiche:oblivious_http_unstable_api",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@rapidjson",
    ],
)

cc_test(
    name = "payload_packaging_test",
    size = "large",
    srcs = [
        "payload_packaging_test.cc",
    ],
    deps = [
        ":payload_packaging_lib",
        "//services/common/compression:gzip",
        "//services/common/test:random",
        "//services/common/util:json_util",
        "//services/seller_frontend_service/util:select_ad_reactor_test_utils",
        "//services/seller_frontend_service/util:web_utils",
        "@com_google_absl//absl/log",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/communication:encoding_utils",
        "@google_privacysandbox_servers_common//src/communication:ohttp_utils",
        "@rapidjson",
    ],
)
