// Copyright 2025 Google LLC
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

#include <fstream>
#include <iterator>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/escaping.h"
#include "benchmark/benchmark.h"
#include "services/common/clients/kv_server/kv_v2.h"
#include "services/common/test/utils/proto_utils.h"
#include "services/common/util/file_util.h"

ABSL_FLAG(std::string, v2_response_input_file, "",
          "Optional. The input file path that contains v2 GetValuesResponse");

namespace privacy_sandbox::bidding_auction_servers {
namespace {
std::string GenerateRandomString(int length) {
  std::string output;
  for (auto i = 0; i < length; i++) {
    output += 'a' + std::rand() % 26;
  }
  return output;
}
// Generates random json string, the resulted size of the string is
// roughly about value_length * wide (ignore key size). The depth determines
// the depth of Json for the level where the value is inserted.
std::string GenerateRandomJsonString(size_t depth, size_t wide,
                                     int value_length) {
  // total size = value_length * wide
  std::string output = "{";
  for (auto i = 0; i < wide; i++) {
    auto json_key = GenerateRandomString(10);
    output += absl::StrFormat(R"(\"%s\")", json_key) + ":";
    std::string value;
    if (depth == 1) {
      value = absl::StrFormat(R"(\"%s\")", GenerateRandomString(value_length));
    } else {
      value = GenerateRandomJsonString(depth - 1, 1, value_length);
    }
    output += value;
    if (i != wide - 1) {
      output += ",";
    }
  }
  output += "}";
  return output;
}
kv_server::v2::GetValuesResponse CreateV2Response(absl::string_view payload) {
  return ParseTextOrDie<kv_server::v2::GetValuesResponse>(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                      absl::CEscape(payload)));
}

kv_server::v2::GetValuesResponse CreateV2ResponseFromRandomJsonValue(
    int json_depth, int json_wide, int value_length) {
  auto value = GenerateRandomJsonString(json_depth, json_wide, value_length);

  const std::string payload = absl::StrFormat(R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello": {
              "value": "%s"
            }
          }
        }
      ]
    }
  ])JSON",
                                              value);

  return ParseTextOrDie<kv_server::v2::GetValuesResponse>(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                      absl::CEscape(payload)));
}

void AddTestV2ResponseFromFile(
    absl::flat_hash_map<std::string, kv_server::v2::GetValuesResponse>&
        result) {
  const std::string file_path = absl::GetFlag(FLAGS_v2_response_input_file);
  if (file_path.empty()) {
    return;
  }
  auto content = GetFileContent(file_path, true);
  if (content.ok()) {
    auto response = CreateV2Response(*content);
    const std::string name = absl::StrCat(
        "BM_V2_Adapter_ResponseLoadedFromFile/size", response.ByteSizeLong());
    result[name] = std::move(response);
  }
}

absl::flat_hash_map<std::string, kv_server::v2::GetValuesResponse>
CreateTestV2ResponseMap() {
  absl::flat_hash_map<std::string, kv_server::v2::GetValuesResponse> result;
  // Add test case from the provided file that contains v2 response
  AddTestV2ResponseFromFile(result);

  // Add random json value test cases
  auto value_length_options = {5, 1000, 5000};
  auto json_wide_options = {1, 5};
  auto json_depth_options = {1, 5};

  for (int l : value_length_options) {
    for (int w : json_wide_options) {
      for (int d : json_depth_options) {
        auto test_response = CreateV2ResponseFromRandomJsonValue(d, w, l);
        auto key = absl::StrCat("BM_V2_Adapter_RandomJsonString/size:",
                                test_response.ByteSizeLong(), "/wide:", w,
                                "/depth:", d);
        result[key] = std::move(test_response);
      }
    }
  }
  return result;
}

void BM_ConvertKvV2ResponseToV1String(
    benchmark::State& state, kv_server::v2::GetValuesResponse v2_response) {
  for (auto _ : state) {
    KVV2AdapterStats v2_adapter_stats;
    benchmark::DoNotOptimize(
        ConvertKvV2ResponseToV1String({"keys"}, v2_response, v2_adapter_stats));
  }
}

void RegisterBenchmarks() {
  auto test_v2_response_map = CreateTestV2ResponseMap();
  for (auto it = test_v2_response_map.begin(); it != test_v2_response_map.end();
       ++it) {
    const auto& key = it->first;
    auto response_node = test_v2_response_map.extract(key);
    auto benchmark = benchmark::RegisterBenchmark(
        response_node.key(), BM_ConvertKvV2ResponseToV1String,
        std::move(response_node.mapped()));
    benchmark->Iterations(1000);
  }
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers

// builders/tools/bazel-debian run -c opt \
// //services/common/clients/kv_server/benchmarking:v2_adapter_benchmarks
int main(int argc, char* argv[]) {
  absl::InitializeLog();
  absl::ParseCommandLine(argc, argv);
  benchmark::Initialize(&argc, argv);
  privacy_sandbox::bidding_auction_servers::RegisterBenchmarks();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
