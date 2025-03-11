// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/util/file_util.h"

#include <fstream>
#include <string>
#include <utility>

#include "absl/log/absl_log.h"
#include "absl/strings/str_cat.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::string> GetFileContent(absl::string_view path,
                                           bool log_on_error) {
  std::ifstream ifs(path.data());
  if (!ifs.good()) {
    std::string err_str = absl::StrCat(kPathFailed, path);
    ABSL_LOG_IF(ERROR, log_on_error) << err_str;
    return absl::InvalidArgumentError(std::move(err_str));
  }
  return std::string(std::istreambuf_iterator<char>(ifs),
                     std::istreambuf_iterator<char>());
}

absl::Status WriteToFile(absl::string_view path, absl::string_view contents,
                         bool log_on_error) {
  std::ofstream ofs(path.data(), std::ios::binary);
  if (!ofs.is_open()) {
    std::string err_str = absl::StrCat(kPathFailed, path);
    ABSL_LOG_IF(ERROR, log_on_error) << err_str;
    return absl::InvalidArgumentError(std::move(err_str));
  }

  ofs << contents;
  ofs.close();
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
