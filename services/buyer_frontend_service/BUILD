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

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//:config.bzl", "ENABLE_CORE_DUMPS_DEFINES", "IS_PROD_BUILD_DEFINES")

cc_library(
    name = "runtime_flags",
    hdrs = [
        "runtime_flags.h",
    ],
    deps = [
        "//services/common/constants:common_service_flags",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "get_bids_unary_reactor",
    srcs = [
        "get_bids_unary_reactor.cc",
    ],
    hdrs = [
        "get_bids_unary_reactor.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/buyer_frontend_service/providers:bidding_signals_providers",
        "//services/buyer_frontend_service/util:bidding_signals",
        "//services/buyer_frontend_service/util:buyer_frontend_utils",
        "//services/common:feature_flags",
        "//services/common/chaffing:transcoding_utils",
        "//services/common/clients/bidding_server:async_client",
        "//services/common/constants:user_error_strings",
        "//services/common/loggers:benchmarking_logger",
        "//services/common/loggers:build_input_process_response_benchmarking_logger",
        "//services/common/metric:server_definition",
        "//services/common/util:async_task_tracker",
        "//services/common/util:cancellation_wrapper",
        "//services/common/util:client_contexts",
        "//services/common/util:json_util",
        "//services/common/util:request_metadata",
        "//services/common/util:request_response_constants",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "buyer_frontend_service",
    srcs = [
        "buyer_frontend_service.cc",
    ],
    hdrs = [
        "buyer_frontend_service.h",
    ],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        ":get_bids_unary_reactor",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/buyer_frontend_service/providers:bidding_signals_providers",
        "//services/buyer_frontend_service/util:bidding_signals",
        "//services/buyer_frontend_service/util:buyer_frontend_utils",
        "//services/common/clients/bidding_server:async_client",
        "//services/common/clients/http:multi_curl_http_fetcher_async",
        "//services/common/metric:server_definition",
        "@com_github_grpc_grpc//:grpc++",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/public/cpio/interface:cpio",
        "@google_privacysandbox_servers_common//src/telemetry",
    ],
)

cc_test(
    name = "get_bids_unary_reactor_test",
    size = "small",
    srcs = [
        "get_bids_unary_reactor_test.cc",
    ],
    deps = [
        ":buyer_frontend_service",
        "//services/buyer_frontend_service/util:bidding_signals",
        "//services/buyer_frontend_service/util:buyer_frontend_test_utils",
        "//services/common/chaffing:transcoding_utils",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_utils",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_binary(
    name = "server",
    srcs = ["buyer_frontend_main.cc"],
    linkopts = [
        "-Wl,-rpath,\\$$ORIGIN/../lib",
    ],
    local_defines = ENABLE_CORE_DUMPS_DEFINES + IS_PROD_BUILD_DEFINES,
    malloc = "@com_google_tcmalloc//tcmalloc",
    visibility = ["//visibility:public"],
    deps = [
        ":buyer_frontend_service",
        ":runtime_flags",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/buyer_frontend_service/providers:bidding_signals_providers",
        "//services/common/clients/config:config_client",
        "//services/common/clients/config:config_client_util",
        "//services/common/concurrent:local_cache",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/telemetry:configure_telemetry",
        "//services/common/util:tcmalloc_utils",
        "@aws_sdk_cpp//:core",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",  # for grpc_cli
        "@com_google_absl//absl/debugging:failure_signal_handler",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/telemetry",
        "@google_privacysandbox_servers_common//src/util:rlimit_core_config",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "buyer_frontend_service_test",
    size = "small",
    srcs = [
        "buyer_frontend_service_test.cc",
    ],
    deps = [
        ":buyer_frontend_service",
        "//services/buyer_frontend_service/util:buyer_frontend_test_utils",
        "//services/common/chaffing:transcoding_utils",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:service_utils",
        "//services/common/test/utils:test_utils",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:scoped_mock_log",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

3
