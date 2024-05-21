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
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_proto_library", "cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")
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
    visibility = [
        "//services/auction_service:__pkg__",
        "//services/auction_service/utils:__pkg__",
    ],
)

cc_library(
    name = "buyer_adtech_reporting_wrapper",
    hdrs = [
        "buyer_adtech_reporting_wrapper.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
    ],
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
        "//services/auction_service/utils:proto_utils",
        "//services/common/clients/code_dispatcher:code_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/constants:user_error_strings",
        "//services/common/encryption:crypto_client_wrapper_interface",
        "//services/common/metric:server_definition",
        "//services/common/reporters:async_reporter",
        "//services/common/util:auction_scope_util",
        "//services/common/util:json_util",
        "//services/common/util:request_response_constants",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/interface:key_fetcher_manager_interface",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
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
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
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
        "//services/common/test:random",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "auction_service_test",
    size = "small",
    srcs = ["auction_service_test.cc"],
    deps = [
        ":auction_service",
        ":score_ads_reactor_test_util",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
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
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/auction_service/code_wrapper:seller_code_wrapper_test",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

proto_library(
    name = "auction_code_fetch_config_proto",
    srcs = ["auction_code_fetch_config.proto"],
    deps = [
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:struct_proto",
    ],
)

cc_proto_library(
    name = "auction_code_fetch_config_cc_proto",
    visibility = ["//services/auction_service:__subpackages__"],
    deps = [":auction_code_fetch_config_proto"],
)

cc_library(
    name = "seller_code_fetch_manager",
    srcs = [
        "seller_code_fetch_manager.cc",
    ],
    hdrs = [
        "seller_code_fetch_manager.h",
    ],
    deps = [
        ":auction_code_fetch_config_cc_proto",
        ":auction_constants",
        "//services/auction_service/code_wrapper:buyer_reporting_fetcher",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/common/clients/code_dispatcher:v8_dispatcher",
        "//services/common/code_fetch:periodic_bucket_fetcher",
        "//services/common/code_fetch:periodic_code_fetcher",
        "//services/common/util:file_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "seller_code_fetch_manager_test",
    size = "small",
    srcs = ["seller_code_fetch_manager_test.cc"],
    deps = [
        ":seller_code_fetch_manager",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/common/clients/code_dispatcher:v8_dispatcher",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/code_fetch:periodic_bucket_fetcher",
        "//services/common/code_fetch:periodic_code_fetcher",
        "//services/common/test:mocks",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface:cpio",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/blob_storage_client:blob_storage_client_mock",
    ],
)

cc_binary(
    name = "server",
    srcs = ["auction_main.cc"],
    linkopts = [
        "-Wl,-rpath,\\$$ORIGIN/../lib",
    ],
    local_defines = ENABLE_CORE_DUMPS_DEFINES,
    visibility = ["//visibility:public"],
    deps = [
        ":auction_code_fetch_config_cc_proto",
        ":auction_service",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/auction_service/benchmarking:score_ads_benchmarking_logger",
        "//services/auction_service/benchmarking:score_ads_no_op_logger",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/auction_service/data:runtime_config",
        "//services/common/clients/config:config_client_util",
        "//services/common/clients/http:multi_curl_http_fetcher_async",
        "//services/common/code_fetch:periodic_code_fetcher",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/telemetry:configure_telemetry",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",  # for grpc_cli
        "@com_google_absl//absl/debugging:failure_signal_handler",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
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
        "@com_google_absl//absl/strings",
    ],
)
