# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_rust//crate_universe:defs.bzl", "crates_repository")

def deps():
    # repin deps using:
    #   EXTRA_DOCKER_RUN_ARGS="--env=CARGO_BAZEL_REPIN=1" builders/tools/bazel-debian sync --only=cddl_crate_index
    crates_repository(
        name = "cddl_crate_index",
        quiet = False,
        cargo_lockfile = Label("cddl/Cargo.lock"),
        lockfile = Label("cddl/cargo-bazel-lock.json"),
        manifests = [
            Label("cddl/Cargo.toml"),
        ],
    )
