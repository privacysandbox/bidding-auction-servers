# Copyright 2022 Google LLC
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

genrule(
    name = "package-lcov-report",
    outs = ["package_lcov_report.bin"],
    cmd_bash = """cat << EOF > '$@'
cp bazel-out/_coverage/_coverage_report.dat dist/_"\\$$@"_coverage_report.dat
EOF""",
    executable = True,
    local = True,
    message = "package lcov report",
)

genrule(
    name = "merge-lcov-reports",
    outs = ["merge_lcov_reports.bin"],
    cmd_bash = """cat << EOF > '$@'
find dist -name '*_coverage_report.dat' -exec echo -a '{}' \\; | \\
xargs builders/tools/lcov -o dist/_combined_coverage_report.dat
EOF""",
    executable = True,
    local = True,
    message = "merge lcov reports",
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
    name = "enable_parc",
    build_setting_default = "false",
    values = [
        "false",
        "true",
    ],
)

config_setting(
    name = "parc_disabled",
    flag_values = {
        ":enable_parc": "false",
    },
    visibility = ["//:__subpackages__"],
)

config_setting(
    name = "parc_enabled",
    flag_values = {
        ":enable_parc": "true",
    },
    visibility = ["//:__subpackages__"],
)

string_flag(
    name = "inference_build",
    build_setting_default = "no",
    values = [
        "yes",
        "no",
    ],
)

string_flag(
    name = "inference_runtime",
    build_setting_default = "noop",
    values = [
        "noop",
        "pytorch",
        "tensorflow",
        "all",
    ],
)

config_setting(
    name = "inference_noop",
    flag_values = {
        ":inference_runtime": "noop",
    },
    visibility = ["//visibility:public"],
)

config_setting(
    name = "inference_pytorch",
    flag_values = {
        ":inference_build": "yes",
        ":inference_runtime": "pytorch",
    },
    visibility = ["//visibility:public"],
)

config_setting(
    name = "inference_tensorflow",
    flag_values = {
        ":inference_build": "yes",
        ":inference_runtime": "tensorflow",
    },
    visibility = ["//visibility:public"],
)

config_setting(
    name = "include_all_inference_binaries",
    flag_values = {
        ":inference_build": "yes",
        ":inference_runtime": "all",
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

filegroup(
    name = "clang_tidy_config",
    srcs = [".clang-tidy"],
    visibility = ["//visibility:public"],
)
