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
load("//:config.bzl", "IS_PROD_BUILD_DEFINES")

package(default_visibility = [
    "//services/common/clients/buyer_frontend_server:__pkg__",
    "//services/seller_frontend_service:__pkg__",
])

cc_library(
    name = "startup_param_parser",
    srcs = [
        "config_param_parser.cc",
    ],
    hdrs = [
        "config_param_parser.h",
    ],
    deps = [
        "//services/common/util:json_util",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/interface:public_key_fetcher_interface",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
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
        ":cbor_common_util",
        "//services/common/compression:gzip",
        "//services/common/util:data_util",
        "//services/common/util:scoped_cbor",
        "//services/seller_frontend_service/private_aggregation:private_aggregation_helper",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/communication:compression",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@libcbor//:cbor",
    ],
)

cc_library(
    name = "cbor_common_util",
    srcs = [
        "cbor_common_util.cc",
    ],
    hdrs = [
        "cbor_common_util.h",
    ],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/compression:gzip",
        "//services/common/util:data_util",
        "//services/common/util:error_accumulator",
        "//services/common/util:request_response_constants",
        "//services/common/util:scoped_cbor",
        "//services/seller_frontend_service/data:seller_frontend_data",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@google_privacysandbox_servers_common//src/communication:compression",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
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
        ":cbor_common_util",
        ":web_utils",
        "//api:bidding_auction_servers_cc_proto_builder",
        "//services/common/private_aggregation:private_aggregation_test_util",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:cbor_test_utils",
        "//services/common/test/utils:proto_utils",
        "//services/common/util:request_response_constants",
        "//services/seller_frontend_service/private_aggregation:private_aggregation_helper",
        "//services/seller_frontend_service/test:constants",
        "//services/seller_frontend_service/test:kanon_test_utils",
        "//services/seller_frontend_service/util:buyer_input_proto_utils",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/core/test/utils",
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
        "//services/common/private_aggregation:private_aggregation_test_util",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:cbor_test_utils",
        "//services/common/test/utils:ohttp_test_utils",
        "//services/common/util:error_accumulator",
        "//services/common/util:oblivious_http_utils",
        "//services/seller_frontend_service",
        "//services/seller_frontend_service/data:seller_frontend_data",
        "//services/seller_frontend_service/k_anon:k_anon_cache_manager_interface",
        "//services/seller_frontend_service/k_anon:k_anon_cache_manager_mock",
        "//services/seller_frontend_service/test:app_test_utils",
        "//services/seller_frontend_service/test:constants",
        "//services/seller_frontend_service/util:framing_utils",
        "@com_github_google_quiche//quiche:oblivious_http_unstable_api",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/communication:encoding_utils",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
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

cc_library(
    name = "key_fetcher_utils",
    srcs = ["key_fetcher_utils.cc"],
    hdrs = ["key_fetcher_utils.h"],
    local_defines = IS_PROD_BUILD_DEFINES,
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//services/common:public_key_url_allowlist",
        "//services/common/clients/config:config_client",
        "//services/common/constants:common_service_flags",
        "//services/common/loggers:request_log_context",
        "//services/common/util:json_util",
        "//services/seller_frontend_service:runtime_flags",
        "//services/seller_frontend_service/util:startup_param_parser",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:fake_key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:private_key_fetcher",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:public_key_fetcher",
    ],
)

cc_test(
    name = "key_fetcher_utils_test",
    srcs = [
        "key_fetcher_utils_test.cc",
    ] + select({
        "//:non_prod_build": ["key_fetcher_utils_non_prod_test.cc"],
        "//conditions:default": ["key_fetcher_utils_prod_test.cc"],
    }),
    local_defines = IS_PROD_BUILD_DEFINES,
    deps = [
        ":key_fetcher_utils",
        "//services/common:public_key_url_allowlist",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "encryption_util",
    srcs = ["encryption_util.cc"],
    hdrs = [
        "encryption_util.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/loggers:request_log_context",
        "//services/common/util:oblivious_http_utils",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/status",
        "@google_privacysandbox_servers_common//src/communication:ohttp_utils",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "encryption_util_test",
    size = "small",
    srcs = ["encryption_util_test.cc"],
    deps = [
        ":encryption_util",
        ":select_ad_reactor_test_utils",
        "//services/common/test:random",
        "@com_google_googletest//:gtest",
        "@google_privacysandbox_servers_common//src/communication:encoding_utils",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:fake_key_fetcher_manager",
    ],
)

cc_library(
    name = "validation_utils",
    srcs = [
        "validation_utils.cc",
    ],
    hdrs = [
        "validation_utils.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/util:error_accumulator",
    ],
)

cc_test(
    name = "validation_utils_test",
    size = "small",
    srcs = [
        "validation_utils_test.cc",
    ],
    deps = [
        ":select_ad_reactor_test_utils",
        ":validation_utils",
        "//services/common/test:random",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "proto_mapping_util",
    srcs = [
        "proto_mapping_util.cc",
    ],
    hdrs = [
        "proto_mapping_util.h",
    ],
    visibility = [
        "//services/seller_frontend_service:__pkg__",
        "//tools/secure_invoke/payload_generator:__pkg__",
    ],
    deps = [
        ":encryption_util",
        ":validation_utils",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/compression:gzip",
        "//services/common/loggers:request_log_context",
        "//services/common/util:error_categories",
        "//services/common/util:hpke_utils",
        "//services/seller_frontend_service/data:seller_frontend_data",
        "//services/seller_frontend_service/util:framing_utils",
        "//services/seller_frontend_service/util:web_utils",
        "@google_privacysandbox_servers_common//src/communication:encoding_utils",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "proto_mapping_util_test",
    size = "small",
    srcs = [
        "proto_mapping_util_test.cc",
    ],
    deps = [
        ":proto_mapping_util",
        ":select_ad_reactor_test_utils",
        "//api:bidding_auction_servers_cc_proto_builder",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/test:random",
        "//services/common/test/utils:ohttp_test_utils",
        "//services/seller_frontend_service/test:constants",
        "//services/seller_frontend_service/test:kanon_test_utils",
        "@com_google_googletest//:gtest",
        "@google_privacysandbox_servers_common//src/core/test/utils",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:fake_key_fetcher_manager",
    ],
)

cc_library(
    name = "buyer_input_proto_utils",
    srcs = [
        "buyer_input_proto_utils.cc",
    ],
    hdrs = [
        "buyer_input_proto_utils.h",
    ],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/util:json_util",
        "@rapidjson",
    ],
)

cc_test(
    name = "buyer_input_proto_utils_test",
    size = "small",
    srcs = [
        "buyer_input_proto_utils_test.cc",
    ],
    deps = [
        ":buyer_input_proto_utils",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "chaffing_utils",
    hdrs = ["chaffing_utils.h"],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)
