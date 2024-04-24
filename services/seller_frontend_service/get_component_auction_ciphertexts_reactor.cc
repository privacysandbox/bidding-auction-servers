// Copyright 2024 Google LLC
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

#include "services/seller_frontend_service/get_component_auction_ciphertexts_reactor.h"

#include <utility>

#include "absl/strings/ascii.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/encryption_util.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

void GetComponentAuctionCiphertextsReactor::Execute() {
  if (seller_cloud_platforms_map_.empty()) {
    FinishWithStatus(grpc::Status(grpc::UNIMPLEMENTED, kDisabledError));
    return;
  }
  // Validate the request
  if (request_->component_sellers().empty() ||
      request_->protected_auction_ciphertext().empty()) {
    FinishWithStatus(
        grpc::Status(grpc::INVALID_ARGUMENT, kEmptyInputFieldError));
    return;
  }

  // Decrypt the protected auction ciphertext
  auto decrypted_data = DecryptOHTTPEncapsulatedHpkeCiphertext(
      request_->protected_auction_ciphertext(), key_fetcher_manager_);
  if (!decrypted_data.ok()) {
    FinishWithStatus(
        grpc::Status(grpc::INVALID_ARGUMENT,
                     absl::StrCat(kCiphertextDecryptionFailureError,
                                  decrypted_data.status().message())));
    return;
  }

  // Re-encrypt the decrypted data for each component seller and add to the
  // response map
  for (const auto& seller : request_->component_sellers()) {
    const auto& seller_cloud_platform_itr =
        seller_cloud_platforms_map_.find(absl::AsciiStrToLower(seller));
    if (seller_cloud_platform_itr == seller_cloud_platforms_map_.end()) {
      // Publish metric here.
      PS_LOG(WARNING, log_context_)
          << "Cloud platform not configured for seller: " << seller;
      continue;
    }

    // Skip duplicates.
    if (response_->mutable_seller_component_ciphertexts()->find(seller) !=
        response_->mutable_seller_component_ciphertexts()->end()) {
      PS_LOG(WARNING, log_context_)
          << "Duplicate component seller in input: " << seller;
      continue;
    }

    absl::StatusOr<OhttpHpkeEncryptedMessage> re_encrypted_data =
        HpkeEncryptAndOHTTPEncapsulate(
            (*decrypted_data)->plaintext, (*decrypted_data)->request_label,
            key_fetcher_manager_, seller_cloud_platform_itr->second);
    if (!re_encrypted_data.ok()) {
      FinishWithStatus(
          grpc::Status(server_common::FromAbslStatus(re_encrypted_data.status())
                           .error_code(),
                       absl::StrCat(kCiphertextEncryptionError,
                                    re_encrypted_data.status().message())));
      return;
    }
    (*response_->mutable_seller_component_ciphertexts())[seller] =
        std::move(re_encrypted_data)->ciphertext;
  }

  PS_VLOG(kSuccess, log_context_) << "Finishing RPC with success.";
  FinishWithStatus(grpc::Status::OK);
}

void GetComponentAuctionCiphertextsReactor::FinishWithStatus(
    const grpc::Status& status) {
  if (status.error_code() != grpc::StatusCode::OK) {
    PS_LOG(ERROR, log_context_) << "RPC failed: " << status.error_message();
  }
  Finish(status);
}

void GetComponentAuctionCiphertextsReactor::OnDone() { delete this; }

void GetComponentAuctionCiphertextsReactor::OnCancel() {
  // TODO: Handle early abort and errors.
}

GetComponentAuctionCiphertextsReactor::GetComponentAuctionCiphertextsReactor(
    const GetComponentAuctionCiphertextsRequest* request,
    GetComponentAuctionCiphertextsResponse* response,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    const absl::flat_hash_map<std::string, server_common::CloudPlatform>&
        seller_cloud_platforms_map)
    : request_(request),
      response_(response),
      key_fetcher_manager_(key_fetcher_manager),
      seller_cloud_platforms_map_(seller_cloud_platforms_map),
      log_context_({}, server_common::ConsentedDebugConfiguration()) {}

}  // namespace privacy_sandbox::bidding_auction_servers
