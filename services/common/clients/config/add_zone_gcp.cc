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

#include <memory>

#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "src/cpio/client_providers/instance_client_provider/gcp/gcp_instance_client_utils.h"

using google::scp::cpio::client_providers::GcpInstanceClientUtils;
using google::scp::cpio::client_providers::GcpInstanceResourceNameDetails;

namespace privacy_sandbox::bidding_auction_servers {
void TrustedServerConfigUtil::ComputeZone(absl::string_view resource_name) {
  GcpInstanceResourceNameDetails details;
  GcpInstanceClientUtils::GetInstanceResourceNameDetails(resource_name,
                                                         details);
  // The zone id is of the form 'us-central1-a'.
  zone_ = details.zone_id;
  // The region is of the form 'us-central1', which is the first 11 characters
  // of the zone id.
  region_ = zone_.substr(0, zone_.size() - 2);
}
absl::btree_map<std::string, std::string>
TrustedServerConfigUtil::GetAttribute() const {
  return {{kZone.data(), TrustedServerConfigUtil::GetZone().data()}};
}
}  // namespace privacy_sandbox::bidding_auction_servers
