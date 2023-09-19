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
#include "glog/logging.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

template <typename RawRequest, typename Request>
absl::StatusOr<std::pair<std::string, std::unique_ptr<Request>>>
EncryptRequestWithHpke(
    std::unique_ptr<RawRequest> raw_request,
    CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    server_common::CloudPlatform cloud_platform) {
  auto key = key_fetcher_manager.GetPublicKey(cloud_platform);
  if (!key.ok()) {
    const std::string error =
        absl::StrCat("Could not get public key to use for HPKE encryption: ",
                     key.status().message());
    LOG(ERROR) << error;
    return absl::InternalError(error);
  }

  auto encrypt_response =
      crypto_client.HpkeEncrypt(key.value(), raw_request->SerializeAsString());

  if (!encrypt_response.ok()) {
    const std::string error = absl::StrCat("Failed encrypting request: ",
                                           encrypt_response.status().message());
    LOG(ERROR) << error;
    return absl::InternalError(error);
  }

  std::unique_ptr<Request> request = std::make_unique<Request>();
  request->set_key_id(key->key_id());
  request->set_request_ciphertext(
      std::move(encrypt_response->encrypted_data().ciphertext()));
  return std::pair{std::move(encrypt_response->secret()), std::move(request)};
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_GRPC_GRPC_CLIENT_UTILS_H_
