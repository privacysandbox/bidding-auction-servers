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

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_proto_library", "cc_test")
load(
    "@rules_pkg//pkg:mappings.bzl",
    "pkg_attributes",
    "pkg_files",
)
load("@rules_proto//proto:defs.bzl", "proto_library")
load("//:config.bzl", "ENABLE_CORE_DUMPS_DEFINES")

cc_library(
    name = "runtime_flags",
    hdrs = [
        "runtime_flags.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/bidding_service/inference:inference_flags",
        "//services/common/constants:common_service_flags",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "buyer_code_fetch_manager",
    srcs = [
        "buyer_code_fetch_manager.cc",
    ],
    hdrs = [
        "buyer_code_fetch_manager.h",
    ],
    visibility = ["//services/bidding_service/byob:__subpackages__"],
    deps = [
        ":bidding_code_fetch_config_cc_proto",
        "//services/bidding_service/code_wrapper:buyer_code_wrapper",
        "//services/common/blob_fetch:fetch_mode_cc_proto",
        "//services/common/clients/code_dispatcher:udf_code_loader_interface",
        "//services/common/data_fetch:periodic_bucket_code_fetcher",
        "//services/common/data_fetch:periodic_code_fetcher",
        "//services/common/loggers:request_log_context",
        "//services/common/util:file_util",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "buyer_code_fetch_manager_test",
    size = "small",
    srcs = ["buyer_code_fetch_manager_test.cc"],
    deps = [
        ":buyer_code_fetch_manager",
        "//services/bidding_service/code_wrapper:buyer_code_wrapper",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/blob_storage_client:blob_storage_client_mock",
    ],
)

cc_library(
    name = "generate_bids_reactor",
    srcs = [
        "generate_bids_reactor.cc",
        "protected_app_signals_generate_bids_reactor.cc",
    ],
    hdrs = [
        "base_generate_bids_reactor.h",
        "generate_bids_reactor.h",
        "protected_app_signals_generate_bids_reactor.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/bidding_service:bidding_constants",
        "//services/bidding_service:egress_schema_cache",
        "//services/bidding_service/benchmarking:bidding_benchmarking_logger",
        "//services/bidding_service/benchmarking:bidding_no_op_logger",
        "//services/bidding_service/code_wrapper:buyer_code_wrapper",
        "//services/bidding_service/data:runtime_config",
        "//services/bidding_service/utils:egress",
        "//services/bidding_service/utils:validation",
        "//services/common:feature_flags",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "//services/common/clients/kv_server:kv_async_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/loggers:request_log_context",
        "//services/common/metric:server_definition",
        "//services/common/util:cancellation_wrapper",
        "//services/common/util:client_contexts",
        "//services/common/util:error_categories",
        "//services/common/util:json_util",
        "//services/common/util:request_metadata",
        "//services/common/util:request_response_constants",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
        "@rapidjson",
    ],
)

cc_test(
    name = "generate_bids_reactor_test",
    size = "small",
    srcs = ["generate_bids_reactor_test.cc"],
    deps = [
        ":generate_bids_reactor",
        "//services/bidding_service:generate_bids_reactor_test_utils",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_library(
    name = "generate_bids_binary_reactor",
    srcs = [
        "generate_bids_binary_reactor.cc",
        "protected_app_signals_generate_bids_binary_reactor.cc",
    ],
    hdrs = [
        "base_generate_bids_reactor.h",
        "generate_bids_binary_reactor.h",
        "protected_app_signals_generate_bids_binary_reactor.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api/udf:generate_bid_sdk_cc_proto",
        "//services/bidding_service/data:runtime_config",
        "//services/bidding_service/utils:validation",
        "//services/common/clients/code_dispatcher:request_context",
        "//services/common/clients/code_dispatcher/byob:byob_dispatch_client",
        "//services/common/code_dispatch:code_dispatch_reactor",
        "//services/common/loggers:request_log_context",
        "//services/common/metric:server_definition",
        "//services/common/util:async_task_tracker",
        "//services/common/util:cancellation_wrapper",
        "//services/common/util:error_categories",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
    ],
)

cc_test(
    name = "generate_bids_binary_reactor_test",
    size = "small",
    srcs = ["generate_bids_binary_reactor_test.cc"],
    deps = [
        ":generate_bids_binary_reactor",
        "//services/bidding_service:generate_bids_reactor_test_utils",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_test(
    name = "bidding_service_test",
    size = "small",
    srcs = ["bidding_service_test.cc"],
    data = [
        ":packaged_cddl_specs",
    ],
    deps = [
        ":bidding_service",
        ":generate_bids_reactor",
        ":generate_bids_reactor_test_utils",
        "//services/bidding_service/benchmarking:bidding_benchmarking_logger",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test/utils:service_utils",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
    ],
)

cc_test(
    name = "bidding_service_integration_test",
    size = "large",
    srcs = [
        "bidding_service_integration_test.cc",
    ],
    tags = [
        "flaky",
    ],
    deps = [
        ":bidding_service",
        ":generate_bids_reactor",
        "//services/bidding_service/benchmarking:bidding_benchmarking_logger",
        "//services/bidding_service/code_wrapper:buyer_code_wrapper",
        "//services/bidding_service/code_wrapper:buyer_code_wrapper_test_constants",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "bidding_service",
    srcs = [
        "bidding_service.cc",
        "bidding_service_factories.h",
    ],
    hdrs = [
        "bidding_service.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":generate_bids_binary_reactor",
        ":generate_bids_reactor",
        ":runtime_flags",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/bidding_service:egress_schema_cache",
        "//services/bidding_service/benchmarking:bidding_no_op_logger",
        "//services/bidding_service/byob:generate_bid_byob_dispatch_client",
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

proto_library(
    name = "bidding_code_fetch_config_proto",
    srcs = ["bidding_code_fetch_config.proto"],
    deps = [
        "//services/common/blob_fetch:fetch_mode_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:struct_proto",
    ],
)

cc_proto_library(
    name = "bidding_code_fetch_config_cc_proto",
    deps = [":bidding_code_fetch_config_proto"],
)

proto_library(
    name = "egress_schema_fetch_config_proto",
    srcs = ["egress_schema_fetch_config.proto"],
    deps = [
        "//services/common/blob_fetch:fetch_mode_proto",
        "@com_google_googleapis//google/api:annotations_proto",
    ],
)

cc_proto_library(
    name = "egress_schema_fetch_config_cc_proto",
    deps = [":egress_schema_fetch_config_proto"],
)

cc_binary(
    name = "server",
    srcs = ["bidding_main.cc"],
    linkopts = [
        "-Wl,-rpath,\\$$ORIGIN/../lib",
    ],
    local_defines = ENABLE_CORE_DUMPS_DEFINES,
    malloc = "@com_google_tcmalloc//tcmalloc",
    visibility = ["//visibility:public"],
    deps = [
        ":bidding_code_fetch_config_cc_proto",
        ":bidding_service",
        ":buyer_code_fetch_manager",
        ":egress_schema_fetch_config_cc_proto",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/bidding_service:bidding_constants",
        "//services/bidding_service:cddl_spec_cache",
        "//services/bidding_service:egress_schema_cache",
        "//services/bidding_service/benchmarking:bidding_benchmarking_logger",
        "//services/bidding_service/byob:buyer_code_fetch_manager_byob",
        "//services/bidding_service/byob:generate_bid_byob_dispatch_client",
        "//services/bidding_service/code_wrapper:buyer_code_wrapper",
        "//services/bidding_service/data:runtime_config",
        "//services/bidding_service/egress_features:adtech_schema_fetcher",
        "//services/bidding_service/inference:inference_utils",
        "//services/bidding_service/inference:model_fetcher_metric",
        "//services/bidding_service/inference:periodic_model_fetcher",
        "//services/common:feature_flags",
        "//services/common/blob_fetch:blob_fetcher",
        "//services/common/clients/config:config_client_util",
        "//services/common/clients/http:multi_curl_http_fetcher_async",
        "//services/common/data_fetch:periodic_code_fetcher",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/telemetry:configure_telemetry",
        "//services/common/util:file_util",
        "//services/common/util:request_response_constants",
        "//services/common/util:tcmalloc_utils",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",  # for grpc_cli
        "@com_google_absl//absl/debugging:failure_signal_handler",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/telemetry",
        "@google_privacysandbox_servers_common//src/util:rlimit_core_config",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@inference_common//sandbox:sandbox_executor",
    ],
)

cc_library(
    name = "bidding_constants",
    hdrs = ["constants.h"],
    visibility = ["//visibility:public"],
    deps = ["@com_google_absl//absl/strings"],
)

cc_test(
    name = "protected_app_signals_generate_bids_reactor_test",
    size = "small",
    srcs = ["protected_app_signals_generate_bids_reactor_test.cc"],
    data = [
        ":packaged_cddl_specs",
    ],
    deps = [
        "//services/bidding_service:generate_bids_reactor",
        "//services/bidding_service:generate_bids_reactor_test_utils",
        "//services/bidding_service/code_wrapper:buyer_code_wrapper",
        "//services/common:feature_flags",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_grpc",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_proto",
    ],
)

cc_library(
    name = "generate_bids_reactor_test_utils",
    testonly = True,
    srcs = ["generate_bids_reactor_test_utils.cc"],
    hdrs = ["generate_bids_reactor_test_utils.h"],
    deps = [
        "//services/bidding_service:bidding_constants",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:crypto_client_wrapper",
        "//services/common/encryption:crypto_client_wrapper_interface",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_proto",
    ],
)

pkg_files(
    name = "packaged_cddl_specs",
    srcs = ["//services/bidding_service/egress_cddl_spec:1.0.0"],
    attributes = pkg_attributes(mode = "0555"),
    prefix = "/services/bidding_service/egress_cddl_spec/",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "cddl_spec_cache",
    srcs = ["cddl_spec_cache.cc"],
    hdrs = ["cddl_spec_cache.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/loggers:request_log_context",
        "//services/common/util:file_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "cddl_spec_cache_test",
    size = "small",
    srcs = ["cddl_spec_cache_test.cc"],
    data = [
        ":packaged_cddl_specs",
    ],
    deps = [
        ":cddl_spec_cache",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "egress_schema_cache",
    srcs = ["egress_schema_cache.cc"],
    hdrs = ["egress_schema_cache.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//services/bidding_service:cddl_spec_cache",
        "//services/bidding_service/egress_features:egress_feature",
        "//services/bidding_service/egress_features:feature_factory",
        "//services/bidding_service/utils:egress",
        "//services/common/loggers:request_log_context",
        "//services/common/util:file_util",
        "//services/common/util:json_util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_test(
    name = "egress_schema_cache_test",
    size = "small",
    srcs = ["egress_schema_cache_test.cc"],
    data = [
        ":packaged_cddl_specs",
    ],
    deps = [
        ":cddl_spec_cache",
        ":egress_schema_cache",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)
