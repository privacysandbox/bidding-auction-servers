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

#include <google/protobuf/util/json_util.h>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "public/applications/pas/retrieval_request_builder.h"
#include "public/applications/pas/retrieval_response_parser.h"
#include "public/query/cpp/client_utils.h"
#include "public/query/v2/get_values_v2.pb.h"
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
  return kv_server::ToJson(kv_server::application_pas::BuildRetrievalRequest(
      retrieval_data,
      {{kClientIp, device_metadata.client_ip},
       {kAcceptLanguage, device_metadata.accept_language},
       {kUserAgent, device_metadata.user_agent}},
      contextual_signals, /*ad_ids=*/{}));
}

absl::StatusOr<AdRetrievalOutput> AdRetrievalOutput::FromJson(
    absl::string_view response_json) {
  kv_server::v2::GetValuesResponse get_values_response;
  PS_RETURN_IF_ERROR(google::protobuf::util::JsonStringToMessage(
      response_json, &get_values_response));

  AdRetrievalOutput ad_retrieval_output;
  PS_ASSIGN_OR_RETURN(
      ad_retrieval_output.ads,
      kv_server::application_pas::GetRetrievalOutput(get_values_response));
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
            PS_VLOG(1)
                << "AdsRetrievalAsyncHttpClient pre-warm returned status:"
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
  PS_VLOG(3) << "Sending the following to ads retrieval service: "
             << request.body;

  // Setup a callback for the returned data from server.
  auto done_callback = [on_done = std::move(on_done)](
                           absl::StatusOr<std::string> result_json) mutable {
    PS_VLOG(3) << "AdsRetrievalKeyValueAsyncHttpClient Response: "
               << result_json.status();
    if (!result_json.ok()) {
      std::move(on_done)(result_json.status());
      return;
    }

    PS_VLOG(3) << "AdsRetrievalKeyValueAsyncHttpClient Response string: "
               << *result_json;
    auto parsed_ad_retrieval_output = AdRetrievalOutput::FromJson(*result_json);
    if (!parsed_ad_retrieval_output.ok()) {
      std::move(on_done)(parsed_ad_retrieval_output.status());
      return;
    }

    std::move(on_done)(std::make_unique<AdRetrievalOutput>(
        *std::move(parsed_ad_retrieval_output)));
  };

  PS_VLOG(3) << "\n\nAds Retrieval Request Url:\n"
             << request.url << "\nHeaders:\n"
             << absl::StrJoin(request.headers, kKeyValDelimiter) << "\nBody: \n"
             << request.body;

  http_fetcher_async_->PutUrl(
      request, static_cast<int>(absl::ToInt64Milliseconds(timeout)),
      std::move(done_callback));
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
