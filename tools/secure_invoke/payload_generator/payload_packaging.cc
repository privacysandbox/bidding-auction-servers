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

#include "tools/secure_invoke/payload_generator/payload_packaging.h"

#include <google/protobuf/util/json_util.h>

#include "absl/container/flat_hash_map.h"
#include "rapidjson/document.h"
#include "services/common/util/json_util.h"
#include "tools/secure_invoke/payload_generator/payload_packaging_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

rapidjson::Document ParseRequestInputJson(absl::string_view json_contents) {
  auto document = ParseJsonString(json_contents);
  CHECK(document.ok()) << document.status();
  CHECK((*document).HasMember(kAuctionConfigField))
      << "Input JSON must contain auction_config";
  CHECK((*document).HasMember(kProtectedAudienceInputField))
      << "Input JSON must contain raw_protected_audience_input";
  CHECK((*document)[kProtectedAudienceInputField].IsObject())
      << "raw_protected_audience_input must be an object";
  // If old buyer input field is present, replace with new field to prevent
  // collision with field in ProtectedAudienceInput while parsing proto.
  if (!(*document)[kProtectedAudienceInputField].HasMember(
          kBuyerInputMapField) &&
      (*document)[kProtectedAudienceInputField].HasMember(
          kOldBuyerInputMapField)) {
    rapidjson::Value& buyer_map =
        (*document)[kProtectedAudienceInputField][kOldBuyerInputMapField];
    (*document)[kProtectedAudienceInputField].AddMember(
        kBuyerInputMapField, buyer_map, document->GetAllocator());
    (*document)[kProtectedAudienceInputField].RemoveMember(
        kOldBuyerInputMapField);
  }
  CHECK(
      (*document)[kProtectedAudienceInputField].HasMember(kBuyerInputMapField))
      << "raw_protected_audience_input must contain buyer input map";
  return std::move(document.value());
}

// Converts rapid json value to json string.
std::string ValueToJson(rapidjson::Value* value) {
  CHECK(value != nullptr) << "Input value must be non null";
  rapidjson::Document doc;
  doc.SetObject();
  doc.CopyFrom(*value, doc.GetAllocator());
  auto json_str = SerializeJsonDoc(doc);
  CHECK(json_str.ok()) << json_str.status();
  return std::move(json_str.value());
}

SelectAdRequest::AuctionConfig GetAuctionConfig(
    rapidjson::Document* input_json) {
  CHECK(input_json != nullptr) << "Input JSON must be non null";
  rapidjson::Value& auction_config_json = (*input_json)[kAuctionConfigField];
  std::string auction_config_json_str = ValueToJson(&auction_config_json);

  SelectAdRequest::AuctionConfig auction_config;
  google::protobuf::json::ParseOptions parse_options;
  parse_options.ignore_unknown_fields = true;
  auto auction_config_parse = google::protobuf::util::JsonStringToMessage(
      auction_config_json_str, &auction_config, parse_options);
  CHECK(auction_config_parse.ok()) << auction_config_parse;
  return auction_config;
}

ProtectedAudienceInput GetProtectedAudienceInput(
    rapidjson::Document* input_json) {
  CHECK(input_json != nullptr) << "Input JSON must be non null";
  rapidjson::Value& protected_audience_json =
      (*input_json)[kProtectedAudienceInputField];
  std::string protected_audience_json_str =
      ValueToJson(&protected_audience_json);

  ProtectedAudienceInput protected_audience_input;
  google::protobuf::json::ParseOptions parse_options;
  parse_options.ignore_unknown_fields = true;
  auto protected_audience_parse = google::protobuf::util::JsonStringToMessage(
      protected_audience_json_str, &protected_audience_input, parse_options);
  CHECK(protected_audience_parse.ok()) << protected_audience_parse;
  return protected_audience_input;
}

google::protobuf::Map<std::string, BuyerInput> GetBuyerInputMap(
    rapidjson::Document* input_json) {
  CHECK(input_json != nullptr) << "Input JSON must be non null";
  CHECK(input_json->HasMember(kProtectedAudienceInputField))
      << "Input Must have field " << kProtectedAudienceInputField;
  CHECK((*input_json)[kProtectedAudienceInputField].HasMember(
      kBuyerInputMapField))
      << "Input " << kProtectedAudienceInputField << " must have field "
      << kBuyerInputMapField;
  rapidjson::Value& buyer_map_json =
      (*input_json)[kProtectedAudienceInputField][kBuyerInputMapField];

  absl::flat_hash_map<std::string, BuyerInput> buyer_input_map;
  for (auto& buyer_input : buyer_map_json.GetObject()) {
    std::string buyer_input_json = ValueToJson(&buyer_input.value);

    google::protobuf::json::ParseOptions parse_options;
    parse_options.ignore_unknown_fields = true;
    BuyerInput buyer_input_proto;
    auto buyer_input_parse = google::protobuf::util::JsonStringToMessage(
        buyer_input_json, &buyer_input_proto, parse_options);
    CHECK(buyer_input_parse.ok()) << buyer_input_parse;

    buyer_input_map.try_emplace(buyer_input.name.GetString(),
                                std::move(buyer_input_proto));
  }

  return google::protobuf::Map<std::string, BuyerInput>(buyer_input_map.begin(),
                                                        buyer_input_map.end());
}

}  // namespace

std::pair<std::unique_ptr<SelectAdRequest>,
          quiche::ObliviousHttpRequest::Context>
PackagePlainTextSelectAdRequest(absl::string_view input_json_str,
                                SelectAdRequest::ClientType client_type) {
  rapidjson::Document input_json = ParseRequestInputJson(input_json_str);
  google::protobuf::Map<std::string, BuyerInput> buyer_map_proto =
      GetBuyerInputMap(&input_json);
  // Encode buyer map.
  absl::StatusOr<google::protobuf::Map<std::string, std::string>>
      encoded_buyer_map;
  switch (client_type) {
    case SelectAdRequest::BROWSER:
      encoded_buyer_map = PackageBuyerInputsForBrowser(buyer_map_proto);
      break;
    case SelectAdRequest::ANDROID:
      encoded_buyer_map = PackageBuyerInputsForApp(buyer_map_proto);
    default:
      break;
  }
  CHECK(encoded_buyer_map.ok()) << encoded_buyer_map.status();

  ProtectedAudienceInput protected_audience_input =
      GetProtectedAudienceInput(&input_json);
  // Set encoded BuyerInput.
  protected_audience_input.mutable_buyer_input()->swap(*encoded_buyer_map);
  // Package protected_audience_input.
  auto pa_ciphertext_encryption_context_pair =
      PackagePayload(protected_audience_input, client_type);
  CHECK(pa_ciphertext_encryption_context_pair.ok())
      << pa_ciphertext_encryption_context_pair.status();
  auto select_ad_request = std::make_unique<SelectAdRequest>();
  *(select_ad_request->mutable_auction_config()) =
      GetAuctionConfig(&input_json);
  select_ad_request->set_protected_audience_ciphertext(
      pa_ciphertext_encryption_context_pair->first);
  select_ad_request->set_client_type(client_type);
  return {std::move(select_ad_request),
          std::move(pa_ciphertext_encryption_context_pair->second)};
}

std::string PackagePlainTextSelectAdRequestToJson(
    absl::string_view input_json_str, SelectAdRequest::ClientType client_type) {
  auto req = std::move(
      PackagePlainTextSelectAdRequest(input_json_str, client_type).first);
  std::string select_ad_json;
  auto select_ad_json_status =
      google::protobuf::util::MessageToJsonString(*req, &select_ad_json);
  CHECK(select_ad_json_status.ok()) << select_ad_json_status;
  return select_ad_json;
}

}  // namespace privacy_sandbox::bidding_auction_servers
