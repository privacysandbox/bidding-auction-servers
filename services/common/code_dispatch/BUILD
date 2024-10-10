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

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "code_dispatch_reactor",
    hdrs = [
        "code_dispatch_reactor.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common:feature_flags",
        "//services/common/clients:async_client",
        "//services/common/constants:user_error_strings",
        "//services/common/encryption:crypto_client_wrapper_interface",
        "//services/common/loggers:request_log_context",
        "//services/common/util:client_contexts",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/interface:key_fetcher_manager_interface",
    ],
)
