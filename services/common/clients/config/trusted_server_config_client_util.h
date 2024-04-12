//  Copyright 2022 Google LLC
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

#ifndef FLEDGE_SERVICES_COMMON_CLIENTS_CONFIG_TRUSTED_SERVER_CONFIG_CLIENT_UTIL_H_  // NOLINT
#define FLEDGE_SERVICES_COMMON_CLIENTS_CONFIG_TRUSTED_SERVER_CONFIG_CLIENT_UTIL_H_  // NOLINT

#include <string>

#include "absl/container/btree_map.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr std::string_view kZone = "zone";

class TrustedServerConfigUtil {
 public:
  // Uses an SCP provided client to parse details of the running instance.
  // TODO(b/279818295): Expand functionality to also work on GCP.
  explicit TrustedServerConfigUtil(bool init_config_client);

  // Returns the server instance id.
  absl::string_view GetInstanceId() const { return instance_id_; }
  // Returns the configured environment.
  absl::string_view GetEnvironment() const { return environment_; }
  // Returns the service that is running.
  absl::string_view GetOperator() const { return operator_; }
  // Returns the component that is running.
  absl::string_view GetService() const { return service_; }

  absl::string_view GetZone() const { return zone_; }

  absl::string_view GetRegion() const { return region_; }

  void ComputeZone(absl::string_view resource_name);

  absl::btree_map<std::string, std::string> GetAttribute() const;

  // Returns a config param prefix that is to be prepended to all keys fetched
  // from the metadata store.
  std::string GetConfigParameterPrefix() noexcept;

 private:
  std::string instance_id_;
  std::string environment_;
  std::string operator_;
  std::string service_;
  std::string zone_;
  std::string region_;
  bool init_config_client_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

// NOLINTNEXTLINE
#endif  // FLEDGE_SERVICES_COMMON_CLIENTS_CONFIG_TRUSTED_SERVER_CONFIG_CLIENT_UTIL_H_
