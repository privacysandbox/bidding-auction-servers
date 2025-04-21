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
load("@rules_proto_grpc//cpp:defs.bzl", "cpp_grpc_library")

package(default_visibility = ["//:__subpackages__"])

cpp_grpc_library(
    name = "parc_service_grpc_proto",
    include_prefix = "apis/privacysandbox/apis/parc/v0",
    protos = [
        "@google_privacysandbox_servers_common//apis/privacysandbox/apis/parc/v0:blob_proto",
        "@google_privacysandbox_servers_common//apis/privacysandbox/apis/parc/v0:parameter_proto",
        "@google_privacysandbox_servers_common//apis/privacysandbox/apis/parc/v0:parc_service_proto",
    ],
)

cc_library(
    name = "parc_parameter_client",
    srcs = ["parc_parameter_client.h"],
    deps = [
        ":parc_service_grpc_proto",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_util",
    ],
)

cc_library(
    name = "config_client_util",
    srcs = ["trusted_server_config_client_util.cc"] + select({
        "@google_privacysandbox_servers_common//:aws_instance": ["add_zone_aws.cc"],
        "@google_privacysandbox_servers_common//:azure_instance": ["add_zone_default.cc"],
        "@google_privacysandbox_servers_common//:gcp_instance": ["add_zone_gcp.cc"],
        "//conditions:default": ["add_zone_default.cc"],
    }),
    hdrs = ["trusted_server_config_client_util.h"],
    deps = [
        "//services/common/loggers:request_log_context",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@google_privacysandbox_servers_common//src/public/core/interface:execution_result",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/instance_client",
    ],
)

cc_library(
    name = "config_client",
    srcs = ["trusted_server_config_client.cc"] + select({
        "//:parc_enabled": ["trusted_server_config_client_parc.cc"],
        "//conditions:default": ["trusted_server_config_client_cpio.cc"],
    }),
    hdrs = ["trusted_server_config_client.h"],
    deps = [
        ":parc_parameter_client",
        "//services/common/constants:common_service_flags",
        "//services/common/loggers:request_log_context",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:str_format",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@google_privacysandbox_servers_common//src/public/core/interface:errors",
        "@google_privacysandbox_servers_common//src/public/cpio/interface/parameter_client",
        "@google_privacysandbox_servers_common//src/telemetry/flag:telemetry_flag",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "config_client_test",
    size = "small",
    srcs = ["trusted_server_config_client_test.cc"],
    deps = [
        ":config_client",
        "//services/common/test:mocks",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/public/cpio/interface:cpio",
        "@google_privacysandbox_servers_common//src/public/cpio/mock/parameter_client:parameter_client_mock",
    ],
)
