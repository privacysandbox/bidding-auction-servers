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

package(
    default_visibility = [
        "//:__subpackages__",
    ],
    licenses = ["notice"],
)

cc_library(
    name = "ohttp_test_utils",
    testonly = True,
    srcs = [
        "ohttp_utils.cc",
    ],
    hdrs = [
        "ohttp_utils.h",
    ],
    deps = [
        "//services/common/util:oblivious_http_utils",
        "@com_github_google_quiche//quiche:oblivious_http_unstable_api",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "cbor_test_utils",
    srcs = ["cbor_test_utils.cc"],
    hdrs = ["cbor_test_utils.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/compression:gzip",
        "//services/common/util:request_response_constants",
        "//services/common/util:scoped_cbor",
        "//services/seller_frontend_service/util:web_utils",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@libcbor//:cbor",
        "@rapidjson",
    ],
)

cc_library(
    name = "service_utils",
    hdrs = ["service_utils.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "test_utils",
    testonly = True,
    srcs = ["test_utils.cc"],
    hdrs = ["test_utils.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/test:random",
    ],
)

cc_library(
    name = "test_init",
    testonly = True,
    hdrs = ["test_init.h"],
    deps = [
        "//services/common/telemetry:configure_telemetry",
        "//services/common/test:random",
        "@com_google_absl//absl/debugging:failure_signal_handler",
    ],
)

cc_library(
    name = "proto_utils",
    testonly = True,
    hdrs = ["proto_utils.h"],
    deps = [
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)
