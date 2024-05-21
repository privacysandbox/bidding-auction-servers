/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>

#ifndef SERVICES_ENCRYPTION_KEY_FETCHER_UTIL_H_
#define SERVICES_ENCRYPTION_KEY_FETCHER_UTIL_H_

#include "services/common/clients/config/trusted_server_config_client.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

std::unique_ptr<server_common::PublicKeyFetcherInterface>
CreatePublicKeyFetcher(TrustedServersConfigClient& config_client);

// Constructs a KeyFetcherManager instance by using TrustedServersConfigClient
// to fetch the required runtime config.
std::unique_ptr<server_common::KeyFetcherManagerInterface>
CreateKeyFetcherManager(
    TrustedServersConfigClient& config_client,
    std::unique_ptr<server_common::PublicKeyFetcherInterface>
        public_key_fetcher);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_ENCRYPTION_KEY_FETCHER_UTIL_H_
