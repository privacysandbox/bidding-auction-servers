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
    name = "seller_frontend_providers",
    srcs = [
        "http_scoring_signals_async_provider.cc",
    ],
    hdrs = [
        "http_scoring_signals_async_provider.h",
        "scoring_signals_async_provider.h",
    ],
    deps = [
        "//services/common/clients/http_kv_server/seller:fake_seller_key_value_async_http_client",
        "//services/common/clients/http_kv_server/seller:seller_key_value_async_http_client",
        "//services/common/providers:async_provider",
        "//services/seller_frontend_service/data:seller_frontend_data",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "seller_frontend_providers_test",
    size = "small",
    srcs = [
        "http_scoring_signals_async_provider_test.cc",
    ],
    deps = [
        "seller_frontend_providers",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest_main",
    ],
)
