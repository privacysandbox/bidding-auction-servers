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

load("@bazel_skylib//rules:copy_file.bzl", "copy_file")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//:config.bzl", "ENABLE_CORE_DUMPS_DEFINES")

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
    name = "auction_constants",
    hdrs = [
        "auction_constants.h",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "auction_service",
    srcs = [
        "auction_service.cc",
    ],
    hdrs = [
        "auction_service.h",
    ],
    deps = [
        ":runtime_flags",
        ":score_ads_reactor",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/common/clients/config:config_client",
        "//services/common/clients/http:multi_curl_http_fetcher_async",
        "//services/common/metric:server_definition",
        "@aws_sdk_cpp//:core",
        "@com_github_grpc_grpc//:grpc++",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/public/cpio/interface:cpio",
        "@google_privacysandbox_servers_common//src/telemetry",
    ],
)

cc_library(
    name = "score_ads_reactor",
    srcs = [
        "score_ads_reactor.cc",
    ],
    hdrs = [
        "score_ads_reactor.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":auction_constants",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/auction_service/data:runtime_config",
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_response",
        "//services/auction_service/reporting/buyer:buyer_reporting_helper",
        "//services/auction_service/reporting/buyer:pa_buyer_reporting_manager",
        "//services/auction_service/reporting/buyer:pas_buyer_reporting_manager",
        "//services/auction_service/reporting/seller:component_seller_reporting_manager",
        "//services/auction_service/reporting/seller:seller_reporting_manager",
        "//services/auction_service/reporting/seller:top_level_seller_reporting_manager",
        "//services/auction_service/utils:proto_utils",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/constants:user_error_strings",
        "//services/common/encryption:crypto_client_wrapper_interface",
        "//services/common/loggers:request_log_context",
        "//services/common/metric:server_definition",
        "//services/common/reporters:async_reporter",
        "//services/common/util:auction_scope_util",
        "//services/common/util:cancellation_wrapper",
        "//services/common/util:json_util",
        "//services/common/util:request_response_constants",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/interface:key_fetcher_manager_interface",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
        "@rapidjson",
    ],
)

cc_test(
    name = "score_ads_reactor_test",
    size = "small",
    srcs = ["score_ads_reactor_test.cc"],
    deps = [
        ":auction_constants",
        ":score_ads_reactor",
        ":score_ads_reactor_test_util",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/auction_service/reporting:reporting_helper_test",
        "//services/auction_service/reporting:reporting_test_util",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_test(
    name = "score_ads_reactor_top_level_auction_test",
    size = "small",
    srcs = ["score_ads_reactor_top_level_auction_test.cc"],
    deps = [
        ":auction_constants",
        ":score_ads_reactor",
        ":score_ads_reactor_test_util",
        "//services/auction_service/code_wrapper:buyer_reporting_udf_wrapper",
        "//services/auction_service/code_wrapper:seller_udf_wrapper",
        "//services/auction_service/reporting:reporting_helper_test",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "auction_service_test",
    size = "small",
    srcs = [
        "auction_service_integration_test_util.cc",
        "auction_service_integration_test_util.h",
        "auction_service_test.cc",
    ],
    deps = [
        ":auction_service",
        ":score_ads_reactor_test_util",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/auction_service/code_wrapper:buyer_reporting_test_constants",
        "//services/auction_service/code_wrapper:seller_udf_wrapper_test_constants",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "auction_service_integration_test",
    size = "large",
    srcs = ["auction_service_integration_test.cc"],
    deps = [
        ":auction_service",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/auction_service/code_wrapper:buyer_reporting_test_constants",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/auction_service/code_wrapper:seller_code_wrapper_test",
        "//services/auction_service/code_wrapper:seller_udf_wrapper",
        "//services/auction_service/code_wrapper:seller_udf_wrapper_test",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "auction_service_reporting_integration_test",
    size = "large",
    srcs = [
        "auction_service_integration_test_util.cc",
        "auction_service_integration_test_util.h",
        "auction_service_reporting_integration_test.cc",
    ],
    deps = [
        ":auction_service",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/auction_service/code_wrapper:buyer_reporting_test_constants",
        "//services/auction_service/code_wrapper:buyer_reporting_udf_wrapper",
        "//services/auction_service/code_wrapper:buyer_reporting_udf_wrapper_test",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/auction_service/code_wrapper:seller_code_wrapper_test",
        "//services/auction_service/code_wrapper:seller_udf_wrapper",
        "//services/auction_service/code_wrapper:seller_udf_wrapper_test",
        "//services/auction_service/reporting:reporting_test_util",
        "//services/auction_service/udf_fetcher:adtech_code_version_util",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_binary(
    name = "server",
    srcs = ["auction_main.cc"],
    linkopts = [
        "-Wl,-rpath,\\$$ORIGIN/../lib",
    ],
    local_defines = ENABLE_CORE_DUMPS_DEFINES,
    malloc = "@com_google_tcmalloc//tcmalloc",
    visibility = ["//visibility:public"],
    deps = [
        ":auction_constants",
        ":auction_service",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/auction_service/data:runtime_config",
        "//services/auction_service/udf_fetcher:auction_code_fetch_config_cc_proto",
        "//services/auction_service/udf_fetcher:seller_udf_fetch_manager",
        "//services/common/clients/config:config_client_util",
        "//services/common/clients/http:multi_curl_http_fetcher_async",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/telemetry:configure_telemetry",
        "//services/common/util:tcmalloc_utils",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",  # for grpc_cli
        "@com_google_absl//absl/debugging:failure_signal_handler",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util:rlimit_core_config",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

# copy "server"'s output as an opaque executable, simulating a binary that's
# not built from source
copy_file(
    name = "server_copy_exe",
    src = ":server",
    out = "server_copy.exe",
    is_executable = True,
)

sh_test(
    name = "server_version_test",
    size = "small",
    srcs = [":server_copy_exe"],
    args = ["--version"],
    tags = [
        "local",  # local precludes remote caching
        "smoke",
    ],
)

cc_library(
    name = "score_ads_reactor_test_util",
    testonly = True,
    srcs = ["score_ads_reactor_test_util.cc"],
    hdrs = ["score_ads_reactor_test_util.h"],
    visibility = [
        "//services/auction_service:__pkg__",
        "//services/auction_service/utils:__pkg__",
    ],
    deps = [
        ":auction_constants",
        ":score_ads_reactor",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/util:proto_util",
        "@com_google_absl//absl/strings",
    ],
)
