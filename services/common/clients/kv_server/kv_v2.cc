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

#include "kv_v2.h"

#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
inline constexpr char kTags[] = "tags";
inline constexpr char kKeyGroupOutputs[] = "keyGroupOutputs";
inline constexpr char kKeyValues[] = "keyValues";

absl::StatusOr<rapidjson::Document> GetIgSignalsForKeyTag(
    std::string_view key_tag, std::vector<rapidjson::Document>& docs) {
  rapidjson::Document ig_signals(rapidjson::kObjectType);
  int compression_group_index = 0;
  for (auto& json : docs) {
    if (!json.IsArray()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Incorrectly formed compression group. Array was expected. "
          "Compression group id %i",
          compression_group_index));
    }
    for (auto& partition_output : json.GetArray()) {
      if (!partition_output.IsObject()) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Incorrectly formed compression group. Compression group id %i",
            compression_group_index));
      }
      auto&& po = partition_output.GetObject();
      auto key_group_outputs = po.FindMember(kKeyGroupOutputs);
      if (!(key_group_outputs != po.MemberEnd() &&
            key_group_outputs->value.IsArray())) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Incorrectly formed compression group. Compression group id %i",
            compression_group_index));
      }
      for (auto& key_group_output : key_group_outputs->value.GetArray()) {
        auto tags = key_group_output.FindMember(kTags);
        if (tags == key_group_output.MemberEnd() || tags->value.Size() != 1 ||
            tags->value[0].GetString() != key_tag) {
          continue;
        }
        auto key_values = key_group_output.FindMember(kKeyValues);
        if (key_values == key_group_output.MemberEnd() ||
            !(key_values->value.IsObject())) {
          return absl::InvalidArgumentError(absl::StrFormat(
              "Incorrectly formed compression group. Compression group id %i",
              compression_group_index));
        }
        for (auto& key_value_pair : key_values->value.GetObject()) {
          if (ig_signals.FindMember(key_value_pair.name) ==
              ig_signals.MemberEnd()) {
            ig_signals.AddMember(key_value_pair.name.Move(),
                                 key_value_pair.value.Move(),
                                 ig_signals.GetAllocator());
          } else {
            PS_VLOG(8) << __func__
                       << "Key value response has multiple different values "
                          "associated"
                          "with the same key: "
                       << key_value_pair.name.GetString();
          }
        }
      }
    }
    compression_group_index++;
  }
  return ig_signals;
}

}  // namespace
absl::StatusOr<std::string> ConvertKvV2ResponseToV1String(
    const std::vector<std::string_view>& key_tags,
    kv_server::v2::GetValuesResponse& v2_response_to_convert) {
  // ParseJsonString doesn't copy, it creates a Document that points to the
  // underlying string. We no longer need the v2_response_to_convert object, so
  // we are ok to directly move the strings from it to the `ig_signals`.
  // However, a doc object for each compression group must exist until
  // SerializeJsonDoc is called. Otherwise the chain of pointers is broken and
  // we get undefined behavior.
  std::vector<rapidjson::Document> docs;
  docs.reserve(v2_response_to_convert.compression_groups().size());
  for (auto&& group : v2_response_to_convert.compression_groups()) {
    PS_ASSIGN_OR_RETURN(auto json, ParseJsonString(group.content()));
    docs.push_back(std::move(json));
  }

  rapidjson::Document top_level_doc(rapidjson::kObjectType);
  absl::flat_hash_map<std::string_view, rapidjson::Document> ig_signals_map;
  for (auto&& key_tag : key_tags) {
    PS_ASSIGN_OR_RETURN(auto ig_signals, GetIgSignalsForKeyTag(key_tag, docs));
    ig_signals_map[key_tag] = std::move(ig_signals);
  }
  for (auto& [key_tag, ig_signals] : ig_signals_map) {
    if (!ig_signals.ObjectEmpty()) {
      top_level_doc.AddMember(
          rapidjson::StringRef(key_tag.data(), key_tag.length()),
          ig_signals.Move(), top_level_doc.GetAllocator());
    }
  }
  if (top_level_doc.ObjectEmpty()) {
    return "";
  }
  return SerializeJsonDoc(top_level_doc);
}

}  // namespace privacy_sandbox::bidding_auction_servers
