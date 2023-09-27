// Copyright 2023 Google LLC
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

#include "services/common/clients/http_kv_server/buyer/ads_retrieval_async_http_client.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "glog/logging.h"
#include "services/common/clients/http_kv_server/buyer/ad_retrieval_constants.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

template <typename T>
rapidjson::Value ToJsonArray(const std::vector<std::string>& input,
                             T& allocator) {
  rapidjson::Value out(rapidjson::kArrayType);
  for (const auto& in : input) {
    rapidjson::Value val(rapidjson::kStringType);
    val.SetString(in.c_str(), in.size(), allocator);
    out.PushBack(val, allocator);
  }
  return out;
}

void PopulateKeyGroup(const std::vector<std::string>& tags,
                      const std::vector<std::string>& keys,
                      rapidjson::Document& key_groups_array) {
  rapidjson::Document key_group_entry(rapidjson::kObjectType);

  // Serialize tags
  rapidjson::Value json_tags =
      ToJsonArray(tags, key_groups_array.GetAllocator());

  // Serialize keys.
  rapidjson::Value json_keys =
      ToJsonArray(keys, key_groups_array.GetAllocator());

  key_group_entry.AddMember(kTags, json_tags, key_groups_array.GetAllocator());
  key_group_entry.AddMember(kKeyList, json_keys,
                            key_groups_array.GetAllocator());

  key_groups_array.PushBack(key_group_entry, key_groups_array.GetAllocator());
}

void PopulateMetadataKeyGroup(const DeviceMetadata& device_metadata,
                              rapidjson::Document& key_groups_array) {
  std::vector<std::string> metadata_key_val;
  if (!device_metadata.client_ip.empty()) {
    metadata_key_val.push_back(
        absl::StrCat(kClientIp, kKeyValDelimiter, device_metadata.client_ip));
  }

  if (!device_metadata.accept_language.empty()) {
    metadata_key_val.push_back(absl::StrCat(kAcceptLanguage, kKeyValDelimiter,
                                            device_metadata.accept_language));
  }

  if (!device_metadata.user_agent.empty()) {
    metadata_key_val.push_back(
        absl::StrCat(kUserAgent, kKeyValDelimiter, device_metadata.user_agent));
  }
  PopulateKeyGroup({kDeviceMetadata}, metadata_key_val, key_groups_array);
}

}  // namespace

absl::StatusOr<std::string> AdRetrievalInput::ToJson() {
  rapidjson::Document key_groups_array(rapidjson::kArrayType);

  if (!protected_signals.empty()) {
    PopulateKeyGroup({kProtectedSignals}, {protected_signals},
                     key_groups_array);
  }

  if (!contextual_signals.empty()) {
    PopulateKeyGroup({kContextualSignals}, {contextual_signals},
                     key_groups_array);
  }

  if (!protected_embeddings.empty()) {
    PopulateKeyGroup({kProtectedEmbeddings}, {protected_embeddings},
                     key_groups_array);
  }

  PopulateMetadataKeyGroup(device_metadata, key_groups_array);

  rapidjson::Document partition_entry(rapidjson::kObjectType);
  partition_entry.AddMember(kKeyGroups, key_groups_array,
                            partition_entry.GetAllocator());
  partition_entry.AddMember(kId, kPartitionIdValue,
                            partition_entry.GetAllocator());

  rapidjson::Document partitions_array(rapidjson::kArrayType);
  partitions_array.PushBack(partition_entry, partitions_array.GetAllocator());

  rapidjson::Document request(rapidjson::kObjectType);
  request.AddMember(kPartitions, partitions_array, request.GetAllocator());

  return SerializeJsonDoc(request);
}

absl::StatusOr<AdRetrievalOutput> AdRetrievalOutput::FromJson(
    absl::string_view response_json) {
  PS_ASSIGN_OR_RETURN(rapidjson::Document document,
                      ParseJsonString(response_json));

  AdRetrievalOutput ad_retrieval_output;
  DCHECK(document.IsObject());
  auto partitions_array_it = document.FindMember(kPartitions);
  if (partitions_array_it == document.MemberEnd()) {
    VLOG(4) << "No partitions found in the ad retrieval response";
    return ad_retrieval_output;
  }

  const auto& partitions_array = partitions_array_it->value;
  if (partitions_array.Size() != 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        kUnexpectedPartitions,
        "Expected exactly one partition, got: ", partitions_array.Size()));
  }

  const auto& partition = partitions_array[0];
  DCHECK(partition.IsObject());
  auto key_group_output_it = partition.FindMember(kKeyGroupOutputs);
  if (key_group_output_it == partition.MemberEnd()) {
    VLOG(4) << "No " << kKeyGroupOutputs << " found in ad retrieval response";
    return ad_retrieval_output;
  }

  const auto& key_group_outputs = key_group_output_it->value;
  DCHECK(key_group_outputs.IsArray());
  const auto& key_group_outputs_array = key_group_outputs.GetArray();
  if (key_group_outputs_array.Empty()) {
    VLOG(4) << kKeyGroupOutputs << " is empty in ad retrieval response";
    return ad_retrieval_output;
  }

  bool found_ads = false;
  bool found_contextual_embeddings = false;
  for (const auto& tags_key_values : key_group_outputs_array) {
    DCHECK(tags_key_values.HasMember(kTags));
    DCHECK(tags_key_values.HasMember(kKeyValues));

    const auto& tags_array = tags_key_values[kTags];
    if (tags_array.Size() != 1) {
      return absl::InvalidArgumentError(absl::StrCat(
          kUnexpectedTags,
          " Expected only one tag to be present, found: ", tags_array.Size()));
    }

    const auto& tag = tags_array[0];
    DCHECK(tag.IsString());
    if (tag == kAds) {
      // Expect only one ads keyGroupOutput.
      DCHECK(!found_ads);
      found_ads = true;
      PS_ASSIGN_OR_RETURN(ad_retrieval_output.ads,
                          SerializeJsonDoc(tags_key_values[kKeyValues]),
                          _ << " while serializing retrieved ads");
    } else if (tag == kContextualEmbeddings) {
      // Expect only one contextual embeddings keyGroupOutput.
      DCHECK(!found_contextual_embeddings);
      found_contextual_embeddings = true;
      PS_ASSIGN_OR_RETURN(
          ad_retrieval_output.contextual_embeddings,
          SerializeJsonDoc(tags_key_values[kKeyValues]),
          _ << " while serializing retrieved contextual embeddings");
    }
  }
  if (!found_ads) {
    VLOG(2) << "No ads found in the ads retrieval response";
  }
  if (!found_contextual_embeddings) {
    VLOG(2) << "No contextual embeddings found in the ads retrieval response";
  }
  return ad_retrieval_output;
}

AdsRetrievalAsyncHttpClient::AdsRetrievalAsyncHttpClient(
    absl::string_view kv_server_base_address,
    std::unique_ptr<HttpFetcherAsync> http_fetcher_async, bool pre_warm)
    : http_fetcher_async_(std::move(http_fetcher_async)),
      kv_server_base_address_(kv_server_base_address) {
  if (pre_warm) {
    auto request = std::make_unique<AdRetrievalInput>();
    Execute(
        std::move(request), {},
        [](absl::StatusOr<std::unique_ptr<AdRetrievalOutput>> ads_kv_output) {
          if (!ads_kv_output.ok()) {
            VLOG(1) << "AdsRetrievalAsyncHttpClient pre-warm returned status:"
                    << ads_kv_output.status();
          }
        },
        // Longer timeout for first request
        absl::Milliseconds(kPreWarmRequestTimeout));
  }
}

absl::Status AdsRetrievalAsyncHttpClient::Execute(
    std::unique_ptr<AdRetrievalInput> keys, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<AdRetrievalOutput>>) &&>
        on_done,
    absl::Duration timeout) const {
  // Setup a request with appropriate URL, headers and body.
  HTTPRequest request = {.url = kv_server_base_address_,
                         .headers = {kApplicationJsonHeader}};
  PS_ASSIGN_OR_RETURN(request.body, keys->ToJson());

  // Setup a callback for the returned data from server.
  auto done_callback = [on_done = std::move(on_done)](
                           absl::StatusOr<std::string> result_json) mutable {
    VLOG(3) << "AdsRetrievalKeyValueAsyncHttpClient Response: "
            << result_json.status();
    if (!result_json.ok()) {
      std::move(on_done)(result_json.status());
      return;
    }

    std::move(on_done)(std::make_unique<AdRetrievalOutput>(
        AdRetrievalOutput({*std::move(result_json)})));
  };

  VLOG(3) << "\n\nAds Retrieval Request Url:\n"
          << request.url << "\nHeaders:\n"
          << absl::StrJoin(request.headers, kKeyValDelimiter) << "\nBody: \n"
          << request.body;

  http_fetcher_async_->PutUrl(
      request, static_cast<int>(absl::ToInt64Milliseconds(timeout)),
      std::move(done_callback));
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
