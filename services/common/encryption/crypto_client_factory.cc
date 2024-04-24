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

#include "services/common/encryption/crypto_client_factory.h"

#include <memory>
#include <utility>

#include "services/common/encryption/crypto_client_wrapper.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "src/public/cpio/interface/crypto_client/crypto_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::crypto_service::v1::HpkeAead;
using ::google::cmrt::sdk::crypto_service::v1::HpkeKdf;
using ::google::cmrt::sdk::crypto_service::v1::HpkeKem;
using ::google::cmrt::sdk::crypto_service::v1::HpkeParams;

std::unique_ptr<CryptoClientWrapperInterface> CreateCryptoClient() {
  HpkeParams hpke_params;
  hpke_params.set_kem(HpkeKem::DHKEM_X25519_HKDF_SHA256);
  hpke_params.set_kdf(HpkeKdf::HKDF_SHA256);
  hpke_params.set_aead(HpkeAead::AES_256_GCM);
  google::scp::cpio::CryptoClientOptions options;
  options.hpke_params = hpke_params;

  std::unique_ptr<google::scp::cpio::CryptoClientInterface> cpio_crypto_client =
      google::scp::cpio::CryptoClientFactory::Create(options);
  cpio_crypto_client->Init().IgnoreError();
  cpio_crypto_client->Run().IgnoreError();
  return std::make_unique<CryptoClientWrapper>(std::move(cpio_crypto_client));
}

}  // namespace privacy_sandbox::bidding_auction_servers
