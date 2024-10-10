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

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "async_client",
    srcs = ["seller_frontend_async_client.cc"],
    hdrs = ["seller_frontend_async_client.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "async_grpc_client_stub_test",
    size = "medium",
    srcs = [
        "seller_frontend_async_grpc_client_test.cc",
    ],
    deps = [
        ":async_client",
        "//services/common/clients/async_grpc:default_raw_async_grpc_client_integration_test_utils",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
