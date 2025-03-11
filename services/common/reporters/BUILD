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
    name = "async_reporter",
    srcs = [
        "async_reporter.cc",
    ],
    hdrs = ["async_reporter.h"],
    deps = [
        "//services/common/clients/http:http_fetcher_async",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "async_reporter_test",
    size = "small",
    srcs = ["async_reporter_test.cc"],
    tags = [
        "requires-network",
    ],
    deps = [
        ":async_reporter",
        "//services/common/clients/http:multi_curl_http_fetcher_async",
        "//services/common/test:constants",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
