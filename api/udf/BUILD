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

load(
    "@google_privacysandbox_servers_common//src/roma/tools/api_plugin:roma_api.bzl",
    "declare_doc",
    "declare_roma_api",
    "roma_byob_sdk",
)
load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "generate_bid_proto",
    srcs = ["generate_bid.proto"],
    visibility = ["//api:__subpackages__"],
    deps = [
        "@google_privacysandbox_servers_common//apis/privacysandbox/apis/roma/app_api/v1:options_proto",
    ],
)

cc_proto_library(
    name = "generate_bid_cc_proto",
    deps = [
        ":generate_bid_proto",
    ],
)

generate_bid_api = declare_roma_api(
    cc_protos = [":generate_bid_cc_proto"],
    proto_basename = "generate_bid",
    protos = [":generate_bid_proto"],
)

generate_bid_guide_intro_text = """
## Introduction

[Bidding service](https://github.com/privacysandbox/protected-auction-services-docs/blob/2e8d1e9f5f4302ea495c5a1a1a852fd9d01cf607/bidding_auction_services_system_design.md#bidding-service)
in the
[B&A TEE based servers](https://github.com/privacysandbox/protected-auction-services-docs/blob/2e8d1e9f5f4302ea495c5a1a1a852fd9d01cf607/bidding_auction_services_system_design.md)
provides a platform for DSPs to execute their bidding function for each Protected Auction interest
group in a trusted execution environment. The DSP bidding function could be implemented in
JavaScript (or WASM driven by JavaScript) conforming to the
[generateBid spec](https://github.com/privacysandbox/protected-auction-services-docs/blob/2e8d1e9f5f4302ea495c5a1a1a852fd9d01cf607/bidding_auction_services_api.md#generatebid)
([as also published](https://github.com/WICG/turtledove/blob/a3640067a102fe0759932cd155933f49f4f3c56d/FLEDGE.md#32-on-device-bidding) in
the Protected Audience web platform explainer).

Bidding service has added support for the execution of a standalone generateBid binary for each
Protected Auction interest group using the
[Roma Bring-Your-Own-Binary (BYOB) interface](https://github.com/privacysandbox/protected-auction-services-docs/blob/679f220dd69879de7cc4ba3b7d53cf0c039f5518/roma_bring_your_own_binary.md).
This enhancement allows developers to implement custom bidding logic beyond the existing JavaScript
(JS) specifications, offering greater control and efficiency.

This SDK provides the specification, documentation, and tooling for developers of the generateBid binary user-defined function (UDF). Refer to the [Onboarding Guide](generateBid%20Onboarding%20Guide.md) to get started.
"""

roma_byob_sdk(
    name = "generate_bid_byob_sdk",
    exclude_tools = True,  # TODO(b/373433607): Remove once approval granted.
    extra_docs = [
        declare_doc(
            doc = "//tools/udf/generate_bid/byob_sdk:onboarding_guide",
            target_subdir = "",
        ),
        declare_doc(
            doc = "//tools/udf/generate_bid/byob_sdk:sample_request_txtpb",
            target_subdir = "requests",
        ),
        declare_doc(
            doc = "//tools/udf/generate_bid/byob_sdk:sample_request_json",
            target_subdir = "requests",
        ),
    ],
    guide_intro_text = generate_bid_guide_intro_text,
    roma_app_api = generate_bid_api,
    visibility = ["//visibility:public"],
)
