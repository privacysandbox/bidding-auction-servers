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
    "//visibility:public",
])

filegroup(
    name = "private_aggregation_js_filegroup",
    srcs = ["//services/bidding_service/code_wrapper/js:private_aggregation_wrapper_bin"],
)

# Generates generated_private_aggregation_wrapper.h file using
# private_aggregation_wrapper.h and replacing the placeholder:
# %JS_CODE_PLACEHOLDER% in private_aggregation_wrapper.h
genrule(
    name = "expand_js_to_header",
    srcs = [
        ":private_aggregation_js_filegroup",
        "private_aggregation_wrapper.h",
    ],
    outs = ["generated_private_aggregation_wrapper.h"],
    cmd = """
    js_file=""
    for file in $(locations :private_aggregation_js_filegroup); do
        if [[ $${file} == *.js ]]; then
            js_file=$${file}
            break
        fi
    done

    if [ -z "$${js_file}" ]; then
        echo "No .js file found" >&2
        exit 1
    fi

    python -c "
import sys

js_file_path = sys.argv[1]
template_file_path = sys.argv[2]
output_file_path = sys.argv[3]

# Read the JavaScript content
with open(js_file_path, 'r') as js_file:
    js_content = js_file.read()

# Read the template file
with open(template_file_path, 'r') as template_file:
    template = template_file.read()

# Perform substitution
output = template.replace('%JS_CODE_PLACEHOLDER%', js_content)

# Write the output
with open(output_file_path, 'w') as output_file:
    output_file.write(output)
    " "$${js_file}" "$(location private_aggregation_wrapper.h)" "$@"
    """,
)

cc_library(
    name = "buyer_code_wrapper",
    srcs = ["buyer_code_wrapper.cc"],
    hdrs = [
        "buyer_code_wrapper.h",
        "generated_private_aggregation_wrapper.h",
    ],
    deps = [
        ":expand_js_to_header",
        "//services/bidding_service:bidding_constants",
        "//services/bidding_service:runtime_flags",
        "//services/common/clients/config:config_client",
        "//services/common/constants:common_service_flags",
        "//services/common/loggers:request_log_context",
        "//services/common/util:reporting_util",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "buyer_code_wrapper_test_constants",
    srcs = ["buyer_code_wrapper.cc"],
    hdrs = [
        "buyer_code_wrapper_test_constants.h",
    ],
    deps = [
        ":buyer_code_wrapper",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "buyer_code_wrapper_test",
    size = "small",
    srcs = [
        "buyer_code_wrapper_test.cc",
        "buyer_code_wrapper_test_constants.h",
        "generated_private_aggregation_wrapper.h",
    ],
    deps = [
        ":buyer_code_wrapper",
        "//services/bidding_service:runtime_flags",
        "//services/common/clients/config:config_client",
        "//services/common/constants:common_service_flags",
        "//services/common/test:mocks",
        "//services/common/util:json_util",
        "//services/common/util:reporting_util",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@rapidjson",
    ],
)
