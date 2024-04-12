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
#include <vector>

#include <rapidjson/error/en.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "rapidjson/document.h"
#include "services/common/util/json_util.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

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
    if (absl::EqualsIgnoreCase(cloud_platform, "GCP")) {
      bfe_endpoint.cloud_platform = server_common::CloudPlatform::kGcp;
    } else if (absl::EqualsIgnoreCase(cloud_platform, "AWS")) {
      bfe_endpoint.cloud_platform = server_common::CloudPlatform::kAws;
    } else if (absl::EqualsIgnoreCase(cloud_platform, "LOCAL")) {
      bfe_endpoint.cloud_platform = server_common::CloudPlatform::kLocal;
    } else {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid value for BFE endpoint cloud platform: ", cloud_platform));
    }

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
    const absl::StatusOr<
        absl::flat_hash_map<std::string, BuyerServiceEndpoint>>&
        ig_owner_to_bfe_domain_map) {
  ABSL_CHECK_OK(ig_owner_to_bfe_domain_map)
      << "Error in fetching IG Owner to BFE domain map.";
  std::vector<std::string> ig_list;
  for (const auto& [ig_owner, bfe_domain] : *ig_owner_to_bfe_domain_map) {
    ig_list.push_back(ig_owner);
  }

  return ig_list;
}

}  // namespace privacy_sandbox::bidding_auction_servers
