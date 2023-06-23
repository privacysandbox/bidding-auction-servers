/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_ENCRYPTION_CRYPTO_CLIENT_FACTORY_H_
#define SERVICES_COMMON_ENCRYPTION_CRYPTO_CLIENT_FACTORY_H_

#include <memory>

#include "services/common/encryption/crypto_client_wrapper_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

// Constructs a CryptoClientWrapper instance.
std::unique_ptr<CryptoClientWrapperInterface> CreateCryptoClient();

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_ENCRYPTION_CRYPTO_CLIENT_FACTORY_H_
