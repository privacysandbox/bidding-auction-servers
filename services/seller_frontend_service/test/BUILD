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

load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//services/seller_frontend_service:__pkg__"])

cc_library(
    name = "app_test_utils",
    testonly = True,
    srcs = ["app_test_utils.cc"],
    hdrs = ["app_test_utils.h"],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/compression:gzip",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
    ],
)

cc_library(
    name = "constants",
    testonly = True,
    hdrs = ["constants.h"],
    visibility = [
        "//services:__subpackages__",
    ],
)

cc_library(
    name = "kanon_test_utils",
    testonly = True,
    srcs = ["kanon_test_utils.cc"],
    hdrs = ["kanon_test_utils.h"],
    visibility = [
        "//services:__subpackages__",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/seller_frontend_service/data:seller_frontend_data",
    ],
)
