// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/encryption/key_fetcher_factory.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/cpp/concurrent/event_engine_executor.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"
#include "src/cpp/encryption/key_fetcher/src/fake_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::privacy_sandbox::server_common::KeyFetcherManagerFactory;
using ::privacy_sandbox::server_common::KeyFetcherManagerInterface;
using ::privacy_sandbox::server_common::PrivateKeyFetcherFactory;
using ::privacy_sandbox::server_common::PrivateKeyFetcherInterface;
using ::privacy_sandbox::server_common::PublicKeyFetcherFactory;
using ::privacy_sandbox::server_common::PublicKeyFetcherInterface;

std::unique_ptr<KeyFetcherManagerInterface> CreateKeyFetcherManager(
    bidding_auction_servers::TrustedServersConfigClient& config_client) {
  if (config_client.GetBooleanParameter(TEST_MODE) ||
      !config_client.GetBooleanParameter(ENABLE_ENCRYPTION)) {
    return std::make_unique<server_common::FakeKeyFetcherManager>();
  }

  absl::string_view public_key_endpoint =
      config_client.GetStringParameter(PUBLIC_KEY_ENDPOINT);
  std::vector<std::string> endpoints = {public_key_endpoint.data()};
  std::unique_ptr<PublicKeyFetcherInterface> public_key_fetcher =
      PublicKeyFetcherFactory::Create(endpoints);

  google::scp::cpio::PrivateKeyVendingEndpoint primary, secondary;
  primary.account_identity =
      config_client.GetStringParameter(PRIMARY_COORDINATOR_ACCOUNT_IDENTITY);
  primary.private_key_vending_service_endpoint =
      config_client.GetStringParameter(
          PRIMARY_COORDINATOR_PRIVATE_KEY_ENDPOINT);
  primary.service_region =
      config_client.GetStringParameter(PRIMARY_COORDINATOR_REGION);

  secondary.account_identity =
      config_client.GetStringParameter(SECONDARY_COORDINATOR_ACCOUNT_IDENTITY);
  secondary.private_key_vending_service_endpoint =
      config_client.GetStringParameter(
          SECONDARY_COORDINATOR_PRIVATE_KEY_ENDPOINT);
  secondary.service_region =
      config_client.GetStringParameter(SECONDARY_COORDINATOR_REGION);

  absl::Duration private_key_ttl = absl::Seconds(
      config_client.GetIntParameter(PRIVATE_KEY_CACHE_TTL_MINUTES));
  std::unique_ptr<PrivateKeyFetcherInterface> private_key_fetcher =
      server_common::PrivateKeyFetcherFactory::Create(primary, {secondary},
                                                      private_key_ttl);

  absl::Duration key_refresh_flow_run_freq = absl::Seconds(
      config_client.GetIntParameter(KEY_REFRESH_FLOW_RUN_FREQUENCY_SECONDS));
  auto event_engine = std::make_unique<server_common::EventEngineExecutor>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  std::unique_ptr<server_common::KeyFetcherManagerInterface> manager =
      KeyFetcherManagerFactory::Create(
          key_refresh_flow_run_freq, std::move(public_key_fetcher),
          std::move(private_key_fetcher), std::move(event_engine));
  manager->Start();

  // TODO(b/280345837): The server should wait till the first key fetch is done
  //  before continuing.
  return manager;
}

}  // namespace privacy_sandbox::bidding_auction_servers
