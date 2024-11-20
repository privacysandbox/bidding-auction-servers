# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "egress_feature",
    srcs = ["egress_feature.cc"],
    hdrs = [
        "egress_feature.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/util:json_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
        "@rapidjson",
    ],
)

cc_library(
    name = "boolean_feature",
    srcs = [
        "boolean_feature.cc",
    ],
    hdrs = [
        "boolean_feature.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":egress_feature",
        "//services/common/util:json_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
    ],
)

cc_test(
    name = "boolean_feature_test",
    size = "small",
    srcs = ["boolean_feature_test.cc"],
    deps = [
        ":boolean_feature",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "unsigned_int_feature",
    srcs = [
        "unsigned_int_feature.cc",
    ],
    hdrs = [
        "unsigned_int_feature.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":egress_feature",
        "//services/bidding_service/utils:egress",
        "//services/common/util:json_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
    ],
)

cc_test(
    name = "unsigned_int_feature_test",
    size = "small",
    srcs = ["unsigned_int_feature_test.cc"],
    deps = [
        ":unsigned_int_feature",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "signed_int_feature",
    srcs = [
        "signed_int_feature.cc",
    ],
    hdrs = [
        "signed_int_feature.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":egress_feature",
        "//services/bidding_service/utils:egress",
        "//services/common/util:json_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
    ],
)

cc_test(
    name = "signed_int_feature_test",
    size = "small",
    srcs = ["signed_int_feature_test.cc"],
    deps = [
        ":signed_int_feature",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "bucket_feature",
    srcs = [
        "bucket_feature.cc",
    ],
    hdrs = [
        "bucket_feature.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":boolean_feature",
        ":egress_feature",
        "//services/common/util:json_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "bucket_feature_test",
    size = "small",
    srcs = ["bucket_feature_test.cc"],
    deps = [
        ":bucket_feature",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "histogram_feature",
    srcs = [
        "histogram_feature.cc",
    ],
    hdrs = [
        "histogram_feature.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":egress_feature",
        ":signed_int_feature",
        ":unsigned_int_feature",
        "//services/bidding_service/utils:egress",
        "//services/common/util:json_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_test(
    name = "histogram_feature_test",
    size = "small",
    srcs = ["histogram_feature_test.cc"],
    deps = [
        ":histogram_feature",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "feature_factory",
    srcs = [
        "feature_factory.cc",
    ],
    hdrs = [
        "feature_factory.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":boolean_feature",
        ":bucket_feature",
        ":egress_feature",
        ":histogram_feature",
        ":signed_int_feature",
        ":unsigned_int_feature",
        "//services/common/util:data_util",
        "//services/common/util:json_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
        "@rapidjson",
    ],
)

cc_test(
    name = "feature_factory_test",
    size = "small",
    srcs = ["feature_factory_test.cc"],
    deps = [
        ":boolean_feature",
        ":bucket_feature",
        ":feature_factory",
        ":histogram_feature",
        ":signed_int_feature",
        ":unsigned_int_feature",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "adtech_schema_fetcher",
    srcs = ["adtech_schema_fetcher.cc"],
    hdrs = ["adtech_schema_fetcher.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//services/bidding_service:egress_schema_cache",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/data_fetch:periodic_url_fetcher",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_test(
    name = "adtech_schema_fetcher_test",
    size = "small",
    srcs = ["adtech_schema_fetcher_test.cc"],
    data = [
        "//services/bidding_service:packaged_cddl_specs",
    ],
    deps = [
        ":adtech_schema_fetcher",
        "//services/bidding_service:cddl_spec_cache",
        "//services/bidding_service:egress_schema_cache",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/test:mocks",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_library(
    name = "egress_schema_fetch_manager",
    srcs = ["egress_schema_fetch_manager.cc"],
    hdrs = ["egress_schema_fetch_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":adtech_schema_fetcher",
        ":egress_schema_bucket_fetcher",
        "//services/bidding_service:egress_schema_cache",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/data_fetch:fetcher_interface",
        "//services/common/data_fetch:version_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_test(
    name = "egress_schema_fetch_manager_test",
    size = "small",
    srcs = ["egress_schema_fetch_manager_test.cc"],
    deps = [
        ":egress_schema_fetch_manager",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/blob_storage_client:blob_storage_client_mock",
    ],
)

cc_library(
    name = "egress_schema_bucket_fetcher",
    srcs = ["egress_schema_bucket_fetcher.cc"],
    hdrs = ["egress_schema_bucket_fetcher.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//services/bidding_service:egress_schema_cache",
        "//services/bidding_service:egress_schema_fetch_config_cc_proto",
        "//services/bidding_service/data:runtime_config",
        "//services/common/data_fetch:periodic_bucket_fetcher",
        "//services/common/data_fetch:version_util",
        "//services/common/loggers:request_log_context",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/blob_storage_client",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_test(
    name = "egress_schema_bucket_fetcher_test",
    size = "small",
    srcs = ["egress_schema_bucket_fetcher_test.cc"],
    data = [
        "//services/bidding_service:packaged_cddl_specs",
    ],
    deps = [
        ":egress_schema_bucket_fetcher",
        "//services/bidding_service:cddl_spec_cache",
        "//services/bidding_service:egress_schema_cache",
        "//services/common/data_fetch:version_util",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/blob_storage_client:blob_storage_client_mock",
    ],
)
