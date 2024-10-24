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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "bidding_signals_providers",
    hdrs = [
        "bidding_signals_async_provider.h",
    ],
    deps = [
        "//services/buyer_frontend_service/data:buyer_frontend_data",
        "//services/common/providers:async_provider",
    ],
)

cc_library(
    name = "http_bidding_signals_providers",
    srcs = [
        "http_bidding_signals_async_provider.cc",
    ],
    hdrs = [
        "bidding_signals_async_provider.h",
        "http_bidding_signals_async_provider.h",
    ],
    deps = [
        "bidding_signals_providers",
        "//services/buyer_frontend_service/data:buyer_frontend_data",
        "//services/common/clients:client_factory_template",
        "//services/common/clients/http_kv_server/buyer:buyer_key_value_async_http_client",
        "//services/common/clients/http_kv_server/buyer:fake_buyer_key_value_async_http_client",
        "//services/common/providers:async_provider",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "http_bidding_signals_providers_test",
    size = "small",
    srcs = [
        "http_bidding_signals_async_provider_test.cc",
    ],
    deps = [
        "http_bidding_signals_providers",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)
