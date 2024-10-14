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

package(
    default_visibility = [
        "//services:__subpackages__",
    ],
    licenses = ["notice"],
)

cc_library(
    name = "json_util",
    hdrs = [
        "json_util.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/status:statusor",
        "@rapidjson",
    ],
)

cc_library(
    name = "proto_util",
    hdrs = [
        "proto_util.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "proto_util_test",
    size = "small",
    srcs = [
        "proto_util_test.cc",
    ],
    deps = [
        ":proto_util",
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "json_util_test",
    size = "small",
    srcs = [
        "json_util_test.cc",
    ],
    deps = [
        ":json_util",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "request_metadata",
    srcs = [
        "request_metadata.h",
    ],
    hdrs = [
        "request_metadata.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/clients:async_client",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "request_metadata_test",
    size = "small",
    srcs = [
        "request_metadata_test.cc",
    ],
    deps = [
        ":request_metadata",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "scoped_cbor",
    hdrs = [
        "scoped_cbor.h",
    ],
    deps = [
        "@libcbor//:cbor",
    ],
)

cc_library(
    name = "request_response_constants",
    hdrs = ["request_response_constants.h"],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "scoped_cbor_test",
    size = "small",
    srcs = [
        "scoped_cbor_test.cc",
    ],
    deps = [
        ":scoped_cbor",
        "@com_google_absl//absl/flags:flag",
        "@com_google_googletest//:gtest_main",
        "@libcbor//:cbor",
    ],
)

cc_library(
    name = "error_categories",
    hdrs = ["error_categories.h"],
    deps = ["@com_google_absl//absl/strings"],
)

cc_library(
    name = "error_reporter",
    hdrs = ["error_reporter.h"],
    deps = [
        ":error_categories",
        "//services/common/loggers:source_location_context",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:source_location",
    ],
)

cc_library(
    name = "error_accumulator",
    srcs = ["error_accumulator.cc"],
    hdrs = ["error_accumulator.h"],
    deps = [
        ":error_categories",
        ":error_reporter",
        "//services/common/loggers:request_log_context",
        "//services/common/loggers:source_location_context",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:source_location",
    ],
)

cc_test(
    name = "error_accumulator_test",
    size = "small",
    srcs = [
        "error_accumulator_test.cc",
    ],
    deps = [
        ":error_accumulator",
        ":error_categories",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "post_auction_signals",
    hdrs = ["post_auction_signals.h"],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "reporting_util",
    srcs = ["reporting_util.cc"],
    hdrs = [
        "reporting_util.h",
    ],
    deps = [
        ":post_auction_signals",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/loggers:request_log_context",
        "//services/common/util:json_util",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_test(
    name = "reporting_util_test",
    size = "small",
    srcs = ["reporting_util_test.cc"],
    deps = [
        ":reporting_util",
        "//services/common/test:random",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "read_system",
    srcs = ["read_system.cc"],
    hdrs = ["read_system.h"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "read_system_test",
    size = "small",
    srcs = [
        "read_system_test.cc",
    ],
    deps = [
        ":read_system",
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "signal_handler",
    hdrs = ["signal_handler.h"],
    deps = [
        "@google_privacysandbox_servers_common//src/util/status_macro:examine_stack",
    ],
)

cc_library(
    name = "async_task_tracker",
    srcs = ["async_task_tracker.cc"],
    hdrs = ["async_task_tracker.h"],
    deps = [
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/util/status_macro:source_location",
    ],
)

cc_test(
    name = "async_task_tracker_test",
    size = "small",
    srcs = [
        "async_task_tracker_test.cc",
    ],
    deps = [
        ":async_task_tracker",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "file_util",
    srcs = [
        "file_util.cc",
    ],
    hdrs = [
        "file_util.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "binary_http_utils",
    hdrs = [
        "binary_http_utils.h",
    ],
    deps = [
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_response_constants",
        "@com_github_google_quiche//quiche:binary_http_unstable_api",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "binary_http_utils_test",
    size = "small",
    srcs = [
        "binary_http_utils_test.cc",
    ],
    deps = [
        ":binary_http_utils",
        "@com_google_googletest//:gtest_main",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_grpc",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_proto",
    ],
)

cc_library(
    name = "auction_scope_util",
    srcs = [
        "auction_scope_util.cc",
    ],
    hdrs = [
        "auction_scope_util.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
    ],
)

cc_test(
    name = "auction_scope_util_test",
    srcs = [
        "auction_scope_util_test.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":auction_scope_util",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "oblivious_http_utils",
    srcs = [
        "oblivious_http_utils.cc",
    ],
    hdrs = [
        "oblivious_http_utils.h",
    ],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/loggers:request_log_context",
        "@com_github_google_quiche//quiche:oblivious_http_unstable_api",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "oblivious_http_utils_test",
    size = "small",
    srcs = [
        "oblivious_http_utils_test.cc",
    ],
    deps = [
        ":oblivious_http_utils",
        "//services/common/test/utils:ohttp_test_utils",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@service_value_key_fledge_privacysandbox//public:constants",
    ],
)

cc_library(
    name = "hpke_utils",
    srcs = ["hpke_utils.cc"],
    hdrs = [
        "hpke_utils.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//services/common/constants:user_error_strings",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:crypto_client_wrapper_interface",
        "//services/common/encryption:key_fetcher_factory",
        "@com_google_absl//absl/status",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
    ],
)

cc_test(
    name = "hpke_utils_test",
    size = "small",
    srcs = [
        "hpke_utils_test.cc",
    ],
    deps = [
        ":hpke_utils",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

genrule(
    name = "generate_build_info",
    outs = ["build_info.cc"],
    cmd = "$(execpath :generate_build_info_cc) >$@",
    # Undocumented attr to depend on workspace status files.
    # Used here because generate_build_info_cc depends on the workspace status files.
    stamp = 1,
    tools = [":generate_build_info_cc"],
    visibility = ["//visibility:private"],
)

cc_library(
    name = "build_info",
    srcs = ["build_info.cc"],
    hdrs = ["build_info.h"],
    visibility = ["//services:__subpackages__"],
    deps = ["@com_google_absl//absl/strings"],
)

cc_library(
    name = "tcmalloc_utils",
    hdrs = [
        "tcmalloc_utils.h",
    ],
    deps = [
        "//services/common/loggers:request_log_context",
        "@com_google_tcmalloc//tcmalloc:malloc_extension",
    ],
)

cc_library(
    name = "data_util",
    hdrs = [
        "data_util.h",
    ],
    deps = [
        "@com_google_absl//absl/strings:string_view",
    ],
)

cc_test(
    name = "data_util_test",
    size = "small",
    srcs = [
        "data_util_test.cc",
    ],
    deps = [
        ":data_util",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "cancellation_wrapper",
    hdrs = ["cancellation_wrapper.h"],
    deps = [
        ":error_categories",
        "//services/common:feature_flags",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "thread_safe_set",
    hdrs = ["thread_safe_set.h"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "thread_safe_set_test",
    size = "small",
    srcs = ["thread_safe_set_test.cc"],
    deps = [
        ":thread_safe_set",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "client_contexts",
    srcs = ["client_contexts.cc"],
    hdrs = ["client_contexts.h"],
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "client_context_util",
    hdrs = ["client_context_util.h"],
    deps = [
        "@com_google_absl//absl/time",
    ],
)
