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

#include <rapidjson/error/en.h>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "rapidjson/document.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<absl::flat_hash_map<std::string, std::string>>
ParseIgOwnerToBfeDomainMap(absl::string_view ig_owner_to_bfe_domain) {
  absl::flat_hash_map<std::string, std::string> ig_owner_to_bfe_domain_map;
  rapidjson::Document ig_owner_to_bfe_domain_json_value;
  if (ig_owner_to_bfe_domain.empty()) {
    return absl::InvalidArgumentError(
        "Empty string for IG Owner to BFE domain map");
  }

  rapidjson::ParseResult parse_result =
      ig_owner_to_bfe_domain_json_value
          .Parse<rapidjson::kParseFullPrecisionFlag>(
              ig_owner_to_bfe_domain.data());
  if (parse_result.IsError()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Malformed IG Owner to BFE domain map, error: ",
                     rapidjson::GetParseError_En(parse_result.Code())));
  }

  if (ig_owner_to_bfe_domain_json_value.MemberCount() < 1) {
    return absl::InvalidArgumentError("Empty IG Owner to BFE domain map");
  }

  for (rapidjson::Value::MemberIterator itr =
           ig_owner_to_bfe_domain_json_value.MemberBegin();
       itr != ig_owner_to_bfe_domain_json_value.MemberEnd(); ++itr) {
    if (!itr->name.IsString()) {
      return absl::InvalidArgumentError(
          "Encountered IG Owner that was not string.");
    }

    const std::string& ig_owner = itr->name.GetString();
    if (ig_owner.empty()) {
      return absl::InvalidArgumentError("Encountered empty IG Owner.");
    }

    if (!itr->value.IsString()) {
      return absl::InvalidArgumentError(
          "Encountered BFE Domain address that was not string.");
    }

    const std::string& bfe_host_addr = itr->value.GetString();
    if (bfe_host_addr.empty()) {
      return absl::InvalidArgumentError(
          "Encountered empty BFE domain address.");
    }

    auto [it_2, inserted] =
        ig_owner_to_bfe_domain_map.try_emplace(ig_owner, bfe_host_addr);
    if (!inserted) {
      return absl::InvalidArgumentError(
          "Entry not inserted into IG_owner->BFE map, check for "
          "duplicate entries.");
    }
  }

  if (ig_owner_to_bfe_domain_map.empty()) {
    return absl::InvalidArgumentError(
        "Zero valid entries for IG Owner to BFE domain map.");
  }
  return ig_owner_to_bfe_domain_map;
}

}  // namespace privacy_sandbox::bidding_auction_servers
