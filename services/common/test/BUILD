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

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "mocks",
    testonly = 1,
    hdrs = ["mocks.h"],
    data = [
        "//services/common/test:grpc_tls",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api/udf:generate_bid_sdk_cc_proto",
        "//services/auction_service:score_ads_reactor",
        "//services/bidding_service:cddl_spec_cache",
        "//services/bidding_service:egress_schema_cache",
        "//services/bidding_service:generate_bids_reactor",
        "//services/common/clients:client_factory_template",
        "//services/common/clients/auction_server:async_client",
        "//services/common/clients/bidding_server:async_client",
        "//services/common/clients/buyer_frontend_server:buyer_frontend_async_client",
        "//services/common/clients/buyer_frontend_server:buyer_frontend_async_client_factory",
        "//services/common/clients/code_dispatcher:udf_code_loader_interface",
        "//services/common/clients/code_dispatcher:v8_dispatch_client",
        "//services/common/clients/code_dispatcher/byob:byob_dispatch_client",
        "//services/common/clients/http:http_fetcher_async",
        "//services/common/concurrent:local_cache",
        "//services/common/providers:async_provider",
        "//services/common/reporters:async_reporter",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_grpc",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_proto",
    ],
)

cc_library(
    name = "random",
    testonly = 1,
    srcs = ["random.cc"],
    hdrs = ["random.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api/udf:generate_bid_sdk_cc_proto",
        "//services/common/test/utils:cbor_test_utils",
        "//services/common/util:request_response_constants",
        "//services/seller_frontend_service/test:app_test_utils",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/time",
    ],
)

filegroup(
    name = "grpc_tls",
    srcs = [
        "artifacts/grpc_tls/localhost.key",
        "artifacts/grpc_tls/localhost.pem",
        "artifacts/grpc_tls/root_certificate_authority.pem",
    ],
)
