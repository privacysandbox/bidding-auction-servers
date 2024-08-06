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

#include "benchmark/request_utils.h"

#include <cstdlib>
#include <regex>
#include <string>
#include <unordered_map>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

RegisterModelRequest CreateRegisterModelRequest(
    const RegisterModelRequest& register_request,
    const std::string& new_model_path) {
  RegisterModelRequest new_register_request = register_request;
  new_register_request.mutable_model_spec()->set_model_path(new_model_path);

  std::unordered_map<std::string, std::string> new_model_files;

  absl::string_view request_model_path =
      register_request.model_spec().model_path();
  for (const auto& [relative_path, file_content] :
       register_request.model_files()) {
    std::string new_file_path = absl::StrCat(
        new_model_path, relative_path.substr(request_model_path.length()));
    new_model_files[new_file_path] = file_content;
  }
  new_register_request.clear_model_files();
  for (const auto& [new_file_path, file_content] : new_model_files) {
    (*new_register_request.mutable_model_files())[new_file_path] = file_content;
  }
  return new_register_request;
}

// Returns an array of random floating numbers.
std::string GenerateRandomFloats(int n) {
  if (n < 1) {
    return "";
  }
  std::string result;
  absl::StrAppend(&result, "\"", GenerateRandomFloat(), "\"");
  for (int i = 1; i < n; i++) {
    absl::StrAppend(&result, ", \"", GenerateRandomFloat(), "\"");
  }
  return result;
}

std::string StringFormat(const std::string& s) {
  std::regex pattern("GenerateRandomFloats\\((\\d+)\\)");
  std::smatch match;
  std::string result = s;

  while (std::regex_search(result, match, pattern)) {
    int num = std::stoi(match.str(1));
    result = std::regex_replace(result, pattern, GenerateRandomFloats(num),
                                std::regex_constants::format_first_only);
  }
  return result;
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
