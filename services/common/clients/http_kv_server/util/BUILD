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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "http_kv_server_gen_url_utils",
    srcs = [
        "generate_url.cc",
    ],
    hdrs = [
        "generate_url.h",
    ],
    deps = [
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/strings",
        "@curl",
    ],
)

cc_library(
    name = "http_kv_server_process_response_utils",
    srcs = [
        "process_response.cc",
    ],
    hdrs = [
        "process_response.h",
    ],
    deps = [
        "//services/common/clients/http:http_fetcher_async",
    ],
)
