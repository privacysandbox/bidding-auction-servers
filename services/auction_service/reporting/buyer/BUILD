# Copyright 2024 Google LLC
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
    name = "pas_buyer_reporting_manager",
    hdrs = [
        "pas_buyer_reporting_manager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_response",
        "//services/common/clients/code_dispatcher:code_dispatch_client",
        "@com_google_absl//absl/status:statusor",
        "@rapidjson",
    ],
)

cc_library(
    name = "pa_buyer_reporting_manager",
    hdrs = [
        "pa_buyer_reporting_manager.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/auction_service/reporting:reporting_helper",
        "//services/auction_service/reporting:reporting_response",
        "//services/common/clients/code_dispatcher:code_dispatch_client",
        "@com_google_absl//absl/status:statusor",
        "@rapidjson",
    ],
)
