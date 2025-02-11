//   Copyright 2022 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include "services/seller_frontend_service/util/config_param_parser.h"

#include <string>
#include <utility>
#include <vector>

#include <rapidjson/error/en.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "rapidjson/document.h"
#include "services/common/util/json_util.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
absl::StatusOr<server_common::CloudPlatform> StringToCloudPlatform(
    absl::string_view cloud_platform) {
  if (absl::EqualsIgnoreCase(cloud_platform, "GCP")) {
    return server_common::CloudPlatform::kGcp;
  } else if (absl::EqualsIgnoreCase(cloud_platform, "AWS")) {
    return server_common::CloudPlatform::kAws;
  } else if (absl::EqualsIgnoreCase(cloud_platform, "LOCAL")) {
    return server_common::CloudPlatform::kLocal;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid value for cloud platform: ", cloud_platform));
  }
}
}  // namespace

absl::StatusOr<absl::flat_hash_map<std::string, BuyerServiceEndpoint>>
ParseIgOwnerToBfeDomainMap(absl::string_view ig_owner_to_bfe_domain) {
  if (ig_owner_to_bfe_domain.empty()) {
    return absl::InvalidArgumentError(
        "Empty string for IG Owner to BFE domain map");
  }

  absl::flat_hash_map<std::string, BuyerServiceEndpoint>
      ig_owner_to_bfe_endpoint_map;
  rapidjson::Document ig_owner_to_bfe_endpoint_json_value;
  rapidjson::ParseResult parse_result =
      ig_owner_to_bfe_endpoint_json_value
          .Parse<rapidjson::kParseFullPrecisionFlag>(
              ig_owner_to_bfe_domain.data());
  if (parse_result.IsError()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Malformed IG Owner to BFE endpoint map, error: ",
                     rapidjson::GetParseError_En(parse_result.Code())));
  }

  if (ig_owner_to_bfe_endpoint_json_value.MemberCount() < 1) {
    return absl::InvalidArgumentError("Empty IG Owner to BFE endpoint map");
  }

  for (rapidjson::Value::MemberIterator itr =
           ig_owner_to_bfe_endpoint_json_value.MemberBegin();
       itr != ig_owner_to_bfe_endpoint_json_value.MemberEnd(); ++itr) {
    if (!itr->name.IsString()) {
      return absl::InvalidArgumentError(
          "Encountered IG Owner that was not string.");
    }

    const std::string& ig_owner = itr->name.GetString();
    if (ig_owner.empty()) {
      return absl::InvalidArgumentError("Encountered empty IG Owner.");
    }

    if (!itr->value.IsObject()) {
      return absl::InvalidArgumentError(
          "Encountered BFE endpoint that was not an object.");
    }

    rapidjson::Value buyer_service_endpoint_value = itr->value.GetObject();
    if (buyer_service_endpoint_value.IsNull()) {
      return absl::InvalidArgumentError("Encountered empty BFE endpoint.");
    }

    BuyerServiceEndpoint bfe_endpoint;
    PS_ASSIGN_OR_RETURN(bfe_endpoint.endpoint,
                        GetStringMember(buyer_service_endpoint_value, "url"));

    PS_ASSIGN_OR_RETURN(
        std::string cloud_platform,
        GetStringMember(buyer_service_endpoint_value, "cloudPlatform"));
    PS_ASSIGN_OR_RETURN(bfe_endpoint.cloud_platform,
                        StringToCloudPlatform(cloud_platform));

    auto [it_2, inserted] =
        ig_owner_to_bfe_endpoint_map.try_emplace(ig_owner, bfe_endpoint);
    if (!inserted) {
      return absl::InvalidArgumentError(
          "Entry not inserted into IG_owner->BFE map, check for "
          "duplicate entries.");
    }
  }

  if (ig_owner_to_bfe_endpoint_map.empty()) {
    return absl::InvalidArgumentError(
        "Zero valid entries for IG Owner to BFE domain map.");
  }
  return ig_owner_to_bfe_endpoint_map;
}

std::vector<std::string> FetchIgOwnerList(
    const absl::flat_hash_map<std::string, BuyerServiceEndpoint>&
        ig_owner_to_bfe_domain_map) {
  std::vector<std::string> ig_list;
  for (const auto& [ig_owner, bfe_domain] : ig_owner_to_bfe_domain_map) {
    ig_list.push_back(ig_owner);
  }

  return ig_list;
}

absl::StatusOr<absl::flat_hash_map<std::string, server_common::CloudPlatform>>
ParseSellerToCloudPlatformInMap(
    absl::string_view seller_to_cloud_platform_json) {
  absl::flat_hash_map<std::string, server_common::CloudPlatform>
      seller_to_cloud_platform;
  if (seller_to_cloud_platform_json.empty()) {
    return seller_to_cloud_platform;
  }
  rapidjson::Document seller_to_cloud_platform_json_d;
  rapidjson::ParseResult parse_result =
      seller_to_cloud_platform_json_d.Parse<rapidjson::kParseFullPrecisionFlag>(
          seller_to_cloud_platform_json.data());
  if (parse_result.IsError()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Malformed Seller to CloudPlatform map, error: ",
                     rapidjson::GetParseError_En(parse_result.Code())));
  }

  for (rapidjson::Value::MemberIterator itr =
           seller_to_cloud_platform_json_d.MemberBegin();
       itr != seller_to_cloud_platform_json_d.MemberEnd(); ++itr) {
    if (!itr->name.IsString()) {
      return absl::InvalidArgumentError(
          "Expected Seller identifier string in key.");
    }

    if (!itr->value.IsString()) {
      return absl::InvalidArgumentError(
          "Expected cloud platform string in value.");
    }

    std::string seller_name = itr->name.GetString();
    if (seller_name.empty()) {
      return absl::InvalidArgumentError("Encountered empty Seller Name.");
    }
    PS_ASSIGN_OR_RETURN(server_common::CloudPlatform platform,
                        StringToCloudPlatform(itr->value.GetString()));
    auto [inserted_itr, inserted_bool] =
        seller_to_cloud_platform.try_emplace(std::move(seller_name), platform);
    if (!inserted_bool) {
      return absl::InvalidArgumentError(
          "Entry not inserted into Seller->Cloud platform map, check for "
          "duplicate entries.");
    }
  }
  return seller_to_cloud_platform;
}

}  // namespace privacy_sandbox::bidding_auction_servers
