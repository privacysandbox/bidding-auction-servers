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

#ifndef SERVICES_COMMON_CLIENTS_ASYNC_GRPC_GRPC_CLIENT_UTILS_H_
#define SERVICES_COMMON_CLIENTS_ASYNC_GRPC_GRPC_CLIENT_UTILS_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/util/hpke_utils.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

template <typename Request>
absl::StatusOr<std::pair<std::string, std::unique_ptr<Request>>>
EncryptRequestWithHpke(
    const std::string& plaintext, CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    server_common::CloudPlatform cloud_platform) {
  PS_ASSIGN_OR_RETURN(HpkeMessage encrypted_request,
                      HpkeEncrypt(plaintext, crypto_client, key_fetcher_manager,
                                  cloud_platform));
  std::unique_ptr<Request> request = std::make_unique<Request>();
  request->set_key_id(std::move(encrypted_request.key_id));
  request->set_request_ciphertext(std::move(encrypted_request.ciphertext));
  return std::pair{std::move(encrypted_request.secret), std::move(request)};
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_GRPC_GRPC_CLIENT_UTILS_H_
