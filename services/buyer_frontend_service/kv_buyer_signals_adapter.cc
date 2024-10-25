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

#include "kv_buyer_signals_adapter.h"

#include <utility>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/common/util/json_util.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using kv_server::UDFArgument;
using std::vector;

namespace {
inline constexpr char kKeyGroupOutputs[] = "keyGroupOutputs";
inline constexpr char kTags[] = "tags";
inline constexpr char kKeys[] = "keys";
inline constexpr char kKeyValues[] = "keyValues";
inline constexpr char kClient[] = "Bna.PA.Buyer.20240930";
inline constexpr char kHostname[] = "hostname";
inline constexpr char kClientType[] = "client_type";
inline constexpr char kExperimentGroupId[] = "experiment_group_id";

kv_server::v2::GetValuesRequest GetRequest(
    const GetBidsRequest::GetBidsRawRequest& get_bids_raw_request) {
  kv_server::v2::GetValuesRequest req;
  req.set_client_version(kClient);
  auto& metadata = *(req.mutable_metadata()->mutable_fields());
  (metadata)[kHostname].set_string_value(get_bids_raw_request.publisher_name());
  (metadata)[kClientType].set_string_value(
      absl::StrCat(get_bids_raw_request.client_type()));
  if (get_bids_raw_request.has_buyer_kv_experiment_group_id()) {
    (metadata)[kExperimentGroupId].set_string_value(
        absl::StrCat(get_bids_raw_request.buyer_kv_experiment_group_id()));
  }
  return req;
}

UDFArgument BuildArgument(vector<std::string> keys) {
  UDFArgument arg;
  arg.mutable_tags()->add_values()->set_string_value(kKeys);
  auto* key_list = arg.mutable_data()->mutable_list_value();
  for (auto& key : keys) {
    key_list->add_values()->set_string_value(std::move(key));
  }
  return arg;
}

absl::Status ValidateInterestGroups(const BuyerInput& buyer_input) {
  if (buyer_input.interest_groups().empty()) {
    return absl::InvalidArgumentError("No interest groups in the buyer input");
  }
  for (auto& ig : buyer_input.interest_groups()) {
    if (!ig.bidding_signals_keys().empty()) {
      return absl::OkStatus();
    }
  }
  return absl::InvalidArgumentError(
      "Interest groups don't have any bidding signals");
}

}  // namespace

absl::StatusOr<std::unique_ptr<BiddingSignals>> ConvertV2BiddingSignalsToV1(
    std::unique_ptr<kv_server::v2::GetValuesResponse> response) {
  rapidjson::Document ig_signals(rapidjson::kObjectType);
  std::vector<rapidjson::Document> docs;
  docs.reserve(response->compression_groups().size());

  // ParseJsonString doesn't copy, it creates a Document that points to the
  // underlying string. We no longer need the response object, so we are ok to
  // directly move the strings from it to the `ig_signals`. However, a doc
  // object for each compression group must exist until SerializeJsonDoc is
  // called. Otherwise the chain of pointers is broken and we get undefined
  // behavior.
  for (auto& group : response->compression_groups()) {
    PS_ASSIGN_OR_RETURN(auto json, ParseJsonString(group.content()));
    docs.push_back(std::move(json));
  }

  int compression_group_index = 0;
  for (auto& json : docs) {
    if (!json.IsArray()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Incorrrectly formed compression group. Array was expected. "
          "Compression group id %i",
          compression_group_index));
    }
    for (auto& partition_output : json.GetArray()) {
      if (!partition_output.IsObject()) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Incorrrectly formed compression group. Compression group id %i",
            compression_group_index));
      }
      auto&& po = partition_output.GetObject();
      auto key_group_outputs = po.FindMember(kKeyGroupOutputs);
      if (!(key_group_outputs != po.MemberEnd() &&
            key_group_outputs->value.IsArray())) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Incorrrectly formed compression group. Compression group id %i",
            compression_group_index));
      }
      for (auto& key_group_output : key_group_outputs->value.GetArray()) {
        auto tags = key_group_output.FindMember(kTags);
        if (tags == key_group_output.MemberEnd() || tags->value.Size() != 1 ||
            std::strcmp(tags->value[0].GetString(), kKeys) != 0) {
          continue;
        }
        auto key_values = key_group_output.FindMember(kKeyValues);
        if (key_values == key_group_output.MemberEnd() ||
            !(key_values->value.IsObject())) {
          return absl::InvalidArgumentError(absl::StrFormat(
              "Incorrrectly formed compression group. Compression group id %i",
              compression_group_index));
        }
        for (auto& key_value_pair : key_values->value.GetObject()) {
          if (ig_signals.FindMember(key_value_pair.name) ==
              ig_signals.MemberEnd()) {
            ig_signals.AddMember(key_value_pair.name,
                                 key_value_pair.value.Move(),
                                 ig_signals.GetAllocator());
          } else {
            PS_VLOG(8)
                << __func__
                << "Key value response has multiple different values assosiated"
                   "with the same key: "
                << key_value_pair.name.GetString();
          }
        }
      }
    }
    compression_group_index++;
  }
  rapidjson::Document top_level_doc(rapidjson::kObjectType);
  top_level_doc.AddMember(kKeys, ig_signals.Move(),
                          top_level_doc.GetAllocator());
  PS_ASSIGN_OR_RETURN(auto trusted_signals, SerializeJsonDoc(top_level_doc));
  return std::make_unique<BiddingSignals>(BiddingSignals{
      std::make_unique<std::string>(std::move(trusted_signals))});
}

absl::StatusOr<std::unique_ptr<kv_server::v2::GetValuesRequest>>
CreateV2BiddingRequest(const BiddingSignalsRequest& bidding_signals_request) {
  auto& bids_request = bidding_signals_request.get_bids_raw_request_;
  PS_RETURN_IF_ERROR(ValidateInterestGroups(bids_request.buyer_input()));
  std::unique_ptr<kv_server::v2::GetValuesRequest> req =
      std::make_unique<kv_server::v2::GetValuesRequest>(
          GetRequest(bids_request));
  {
    *req->mutable_consented_debug_config() =
        bids_request.consented_debug_config();
  }
  { *req->mutable_log_context() = bids_request.log_context(); }
  int compression_and_partition_id = 0;
  // TODO (b/369181315): this needs to be reworked to include multiple IGs's
  // keys per partition.
  for (auto& ig : bids_request.buyer_input().interest_groups()) {
    kv_server::v2::RequestPartition* partition = req->add_partitions();
    for (auto& key : ig.bidding_signals_keys()) {
      std::vector<std::string> keys;
      keys.push_back(key);
      *partition->add_arguments() = BuildArgument(std::move(keys));
      partition->set_id(compression_and_partition_id);
      partition->set_compression_group_id(compression_and_partition_id);
    }
    compression_and_partition_id++;
  }
  PS_VLOG(8) << __func__
             << " Created KV lookup request: " << req->DebugString();
  return req;
}

}  // namespace privacy_sandbox::bidding_auction_servers
