# Copyright 2025 Google LLC
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

# Generates tld.h and tld_data.c by building libtld with the instructions: https://github.com/m2osw/libtld/blob/main/INSTALL.txt#building
genrule(
    name = "libtld_srcs",
    srcs = [
        ":Dockerfile.libtld",
    ],
    outs = [
        "tld.cpp",
        "tld_data.c",
        "tld_domain_to_lowercase.c",
        "tld_file.cpp",
        "tld_strings.c",
        "tld.h",
        "tld_data.h",
        "tld_file.h",
    ],
    cmd_bash = """
set -o errexit
cp $(SRCS) "$(RULEDIR)"
docker buildx build --progress plain --target libtld-output --output="$(RULEDIR)" --file="$(RULEDIR)/Dockerfile.libtld" "$(RULEDIR)"
""",
)

cc_library(
    name = "libtld",
    srcs = [
        "tld.cpp",
        "tld_data.c",
        "tld_domain_to_lowercase.c",
        "tld_file.cpp",
        "tld_strings.c",
    ],
    hdrs = [
        "tld.h",
        "tld_data.h",
        "tld_file.h",
    ],
    include_prefix = "libtld",
    visibility = ["//visibility:public"],
    deps = [
        ":libtld_srcs",
    ],
)
