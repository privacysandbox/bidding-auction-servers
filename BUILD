# Copyright 2022 Google LLC
# Copyright (C) Microsoft Corporation. All rights reserved.
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

load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

package(default_visibility = ["//visibility:public"])

string_flag(
    name = "instance",
    build_setting_default = "aws",
    values = [
        "aws",
        "azure",
        "gcp",
        "local",
    ],
)

genrule(
    name = "precommit-hooks",
    outs = ["run_precommit_hooks.bin"],
    cmd = """cat << EOF > '$@'
builders/tools/pre-commit
EOF""",
    executable = True,
    local = True,
)

genrule(
    name = "buildifier",
    outs = ["run_buildifier.bin"],
    cmd = """cat << EOF > '$@'
builders/tools/pre-commit buildifier
EOF""",
    executable = True,
    local = True,
)

# Config settings to determine which platform the system will be built to run on
# Example:
string_flag(
    name = "platform",
    build_setting_default = "aws",
    values = [
        "aws",
        "azure",
        "gcp",
        "local",
    ],
)

config_setting(
    name = "aws_platform",
    flag_values = {
        ":platform": "aws",
    },
    visibility = ["//visibility:private"],
)

config_setting(
    name = "azure_platform",
    flag_values = {
        ":platform": "azure",
    },
    visibility = ["//visibility:private"],
)

config_setting(
    name = "gcp_platform",
    flag_values = {
        ":platform": "gcp",
    },
    visibility = ["//visibility:private"],
)

config_setting(
    name = "gcp_instance",
    flag_values = {
        ":instance": "gcp",
    },
    visibility = ["//visibility:public"],
)

config_setting(
    name = "aws_instance",
    flag_values = {
        ":instance": "aws",
    },
    visibility = ["//visibility:public"],
)

config_setting(
    name = "local_platform",
    flag_values = {
        ":platform": "local",
    },
    visibility = ["//visibility:private"],
)

exports_files(
    [".bazelversion"],
)

genrule(
    name = "collect-logs",
    outs = ["collect_logs.bin"],
    cmd_bash = """cat << EOF > '$@'
tools/collect-logs "\\$$@"
EOF""",
    executable = True,
    local = True,
    message = "copy bazel build and test logs",
)

genrule(
    name = "collect-coverage",
    outs = ["collect_coverage.bin"],
    cmd_bash = """cat << EOF > '$@'
builders/tools/collect-coverage "\\$$@"
EOF""",
    executable = True,
    local = True,
    message = "generate coverage report",
)

string_flag(
    name = "build_flavor",
    build_setting_default = "prod",
    values = [
        "prod",
        "non_prod",
    ],
)

config_setting(
    name = "non_prod_build",
    flag_values = {
        ":build_flavor": "non_prod",
    },
    visibility = ["//visibility:public"],
)

string_flag(
    name = "build_for_test",
    build_setting_default = "non_test",
    values = [
        "non_test",
        "e2e",
    ],
)

config_setting(
    name = "e2e_build",
    flag_values = {
        ":build_for_test": "e2e",
    },
    visibility = ["//visibility:public"],
)

filegroup(
    name = "version_data",
    srcs = ["version.txt"],
    visibility = ["//services/common/telemetry:__pkg__"],
)
