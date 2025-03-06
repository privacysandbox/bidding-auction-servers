# Copyright 2023 Google LLC
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

load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "select_ad_reactor_benchmarks",
    testonly = True,
    srcs = [
        "select_ad_reactor_benchmarks.cc",
    ],
    linkopts = [
        "-Wl,-rpath,\\$$ORIGIN/../lib",
    ],
    deps = [
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "//services/seller_frontend_service",
        "//services/seller_frontend_service/util:select_ad_reactor_test_utils",
        "@google_benchmark//:benchmark",
        "@google_benchmark//:benchmark_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)
