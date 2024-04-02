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
#include "src/cpio/client_providers/instance_client_provider/aws/aws_instance_client_utils.h"

using google::scp::cpio::client_providers::AwsInstanceClientUtils;
using google::scp::cpio::client_providers::AwsResourceNameDetails;

namespace privacy_sandbox::bidding_auction_servers {
void TrustedServerConfigUtil::ComputeZone(absl::string_view resource_name) {
  AwsResourceNameDetails details;
  AwsInstanceClientUtils::GetResourceNameDetails(resource_name, details);
  region_ = details.region;
}
absl::btree_map<std::string, std::string>
TrustedServerConfigUtil::GetAttribute() const {
  return {};
}
}  // namespace privacy_sandbox::bidding_auction_servers
