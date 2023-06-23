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
    name = "code_fetcher_interface",
    hdrs = ["code_fetcher_interface.h"],
)

cc_library(
    name = "periodic_code_fetcher",
    srcs = ["periodic_code_fetcher.cc"],
    hdrs = ["periodic_code_fetcher.h"],
    deps = [
        ":code_fetcher_interface",
        "//services/common/clients:http_fetcher_async",
        "//services/common/clients/code_dispatcher:v8_dispatcher",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/cpp/concurrent:executor",
    ],
)

cc_test(
    name = "periodic_code_fetcher_test",
    size = "small",
    srcs = ["periodic_code_fetcher_test.cc"],
    deps = [
        ":code_fetcher_interface",
        ":periodic_code_fetcher",
        "//services/common/clients:http_fetcher_async",
        "//services/common/clients/code_dispatcher:v8_dispatcher",
        "//services/common/test:mocks",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/cpp/concurrent:executor",
    ],
)
