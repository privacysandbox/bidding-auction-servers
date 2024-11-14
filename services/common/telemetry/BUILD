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

package(default_visibility = [
    "//services:__subpackages__",
])

cc_library(
    name = "configure_telemetry",
    hdrs = [
        "configure_telemetry.h",
    ],
    deps = [
        "//services/common/clients/config:config_client",
        "//services/common/clients/config:config_client_util",
        "//services/common/constants:common_service_flags",
        "//services/common/metric:server_definition",
        "//services/common/util:build_info",
        "@google_privacysandbox_servers_common//src/telemetry",
        "@io_opentelemetry_cpp//sdk/src/resource",
    ],
)

cc_test(
    name = "configure_telemetry_test",
    timeout = "short",
    srcs = ["configure_telemetry_test.cc"],
    data = ["//:version_data"],
    deps = [
        ":configure_telemetry",
        "//services/common/test/utils:test_init",
        "@com_google_googletest//:gtest_main",
    ],
)
