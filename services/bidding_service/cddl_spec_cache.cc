//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/bidding_service/cddl_spec_cache.h"

#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "services/common/util/file_util.h"
#include "src/logger/request_context_impl.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kEgressSpecPath = "/egress_cddl_spec/";
inline constexpr int kNumSupportedCddlSpecs = 1;
inline constexpr std::array<absl::string_view, kNumSupportedCddlSpecs>
    kSupportedCddlVersions = {
        "1.0.0",
};

CddlSpecCache::CddlSpecCache(absl::string_view dir_path)
    : cddl_spec_dir_path_(std::string(dir_path)) {}

absl::Status CddlSpecCache::Init() {
  // Load all the specs here.
  for (const auto& version : kSupportedCddlVersions) {
    auto contents = GetFileContent(absl::StrCat(cddl_spec_dir_path_, version),
                                   /*log_on_error=*/true);
    if (!contents.ok()) {
      PS_VLOG(5) << "Unable to load CDDL spec version: " << version << " : "
                 << contents.status();
      continue;
    }
    PS_VLOG(5) << "Loaded CDDL spec version: " << version << " :\n"
               << *contents;
    version_contents_[version] = *std::move(contents);
  }
  return absl::OkStatus();
}

absl::StatusOr<absl::string_view> CddlSpecCache::Get(
    absl::string_view version) const {
  auto it = version_contents_.find(version);
  if (it == version_contents_.end()) {
    std::string err = absl::StrCat(
        "Unable to find the CDDL spec version in cache: ", version);
    PS_VLOG(5) << err;
    return absl::NotFoundError(std::move(err));
  }

  return it->second;
}

}  // namespace privacy_sandbox::bidding_auction_servers
