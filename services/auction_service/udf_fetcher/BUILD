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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_proto_library", "cc_test")
load(
    "@rules_pkg//pkg:mappings.bzl",
    "pkg_attributes",
    "pkg_files",
)
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "buyer_reporting_fetcher",
    srcs = ["buyer_reporting_fetcher.cc"],
    hdrs = [
        "buyer_reporting_fetcher.h",
    ],
    deps = [
        ":auction_code_fetch_config_cc_proto",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/data_fetch:fetcher_interface",
        "//services/common/loggers:request_log_context",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_library(
    name = "buyer_reporting_udf_fetch_manager",
    srcs = ["buyer_reporting_udf_fetch_manager.cc"],
    hdrs = [
        "buyer_reporting_udf_fetch_manager.h",
    ],
    deps = [
        ":adtech_code_version_util",
        ":auction_code_fetch_config_cc_proto",
        "//services/common/clients/code_dispatcher:v8_dispatcher",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/data_fetch:fetcher_interface",
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

proto_library(
    name = "auction_code_fetch_config_proto",
    srcs = ["auction_code_fetch_config.proto"],
    deps = [
        "//services/common/blob_fetch:fetch_mode_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:struct_proto",
    ],
)

cc_proto_library(
    name = "auction_code_fetch_config_cc_proto",
    deps = [":auction_code_fetch_config_proto"],
)

cc_library(
    name = "adtech_code_version_util",
    srcs = [
        "adtech_code_version_util.cc",
    ],
    hdrs = [
        "adtech_code_version_util.h",
    ],
    deps = [
        "//services/auction_service:auction_constants",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/strings",
        "@curl",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_test(
    name = "adtech_code_version_util_test",
    srcs = [
        "adtech_code_version_util_test.cc",
    ],
    deps = [
        ":adtech_code_version_util",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "seller_udf_fetch_manager",
    srcs = [
        "seller_udf_fetch_manager.cc",
    ],
    hdrs = [
        "seller_udf_fetch_manager.h",
    ],
    deps = [
        ":auction_code_fetch_config_cc_proto",
        ":buyer_reporting_fetcher",
        ":buyer_reporting_udf_fetch_manager",
        "//services/auction_service:auction_constants",
        "//services/auction_service/code_wrapper:buyer_reporting_udf_wrapper",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/auction_service/code_wrapper:seller_udf_wrapper",
        "//services/auction_service/data:runtime_config",
        "//services/common/clients/code_dispatcher:v8_dispatcher",
        "//services/common/data_fetch:periodic_bucket_code_fetcher",
        "//services/common/data_fetch:periodic_code_fetcher",
        "//services/common/data_fetch:version_util",
        "//services/common/loggers:request_log_context",
        "//services/common/util:file_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

exports_files(["testScoreAds.js"])

pkg_files(
    name = "packaged_scoreAds",
    srcs = [":testScoreAds.js"],
    attributes = pkg_attributes(mode = "0555"),
    prefix = "/services/auction_service/udf_fetcher/",
    visibility = ["//visibility:public"],
)

cc_test(
    name = "seller_udf_fetch_manager_test",
    size = "small",
    srcs = ["seller_udf_fetch_manager_test.cc"],
    data = [
        ":packaged_scoreAds",
    ],
    deps = [
        ":seller_udf_fetch_manager",
        "//services/auction_service/code_wrapper:buyer_reporting_udf_wrapper",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/auction_service/code_wrapper:seller_udf_wrapper",
        "//services/common/clients/code_dispatcher:v8_dispatcher",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/data_fetch:periodic_bucket_code_fetcher",
        "//services/common/data_fetch:periodic_code_fetcher",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
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

cc_test(
    name = "buyer_reporting_udf_fetch_manager_test",
    size = "small",
    srcs = ["buyer_reporting_udf_fetch_manager_test.cc"],
    deps = [
        ":buyer_reporting_udf_fetch_manager",
        ":seller_udf_fetch_manager",
        "//services/auction_service/code_wrapper:seller_code_wrapper",
        "//services/common/clients/code_dispatcher:v8_dispatcher",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/data_fetch:periodic_bucket_code_fetcher",
        "//services/common/data_fetch:periodic_code_fetcher",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/core/interface:async_context",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface:cpio",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/blob_storage_client:blob_storage_client_mock",
    ],
)
