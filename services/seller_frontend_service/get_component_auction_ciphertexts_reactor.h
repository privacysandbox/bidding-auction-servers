/*
 * Copyright 2024 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_GET_COMPONENT_AUCTION_CIPHERTEXTS_REACTOR_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_GET_COMPONENT_AUCTION_CIPHERTEXTS_REACTOR_H_

#include <string>

#include <grpcpp/grpcpp.h>

#include "absl/container/flat_hash_map.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "include/grpcpp/impl/codegen/server_callback.h"
#include "services/common/loggers/request_log_context.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

// Constants for service errors.
inline constexpr char kDisabledError[] =
    "RPC disabled: Seller cloud platforms must "
    "be configured for RPC to be enabled.";
inline constexpr char kEmptyInputFieldError[] =
    "Component sellers list or ciphertext is empty";
inline constexpr char kCiphertextDecryptionFailureError[] =
    "Failed to decrypt ciphertext: ";
inline constexpr char kCiphertextEncryptionError[] =
    "Failed to re-encrypt data with error: ";

class GetComponentAuctionCiphertextsReactor : public grpc::ServerUnaryReactor {
 public:
  explicit GetComponentAuctionCiphertextsReactor(
      const GetComponentAuctionCiphertextsRequest* request,
      GetComponentAuctionCiphertextsResponse* response,
      server_common::KeyFetcherManagerInterface& key_fetcher_manager,
      // The keys(seller name/domain) must be lower case.
      // This is currently performed while parsing the seller platform maps
      // config obtained from the parameter store.
      const absl::flat_hash_map<std::string, server_common::CloudPlatform>&
          seller_cloud_platforms_map);
  void Execute();

 private:
  const GetComponentAuctionCiphertextsRequest* request_;
  GetComponentAuctionCiphertextsResponse* response_;
  server_common::KeyFetcherManagerInterface& key_fetcher_manager_;
  const absl::flat_hash_map<std::string, server_common::CloudPlatform>&
      seller_cloud_platforms_map_;
  RequestLogContext log_context_;

  // Cleans up and deletes the GetComponentAuctionCiphertextsReactor. Called by
  // the grpc library after the response has finished.
  void OnDone() override;

  // Abandons the entire GetComponentAuctionCiphertextsReactor. Called by the
  // grpc library if the client cancels the request.
  void OnCancel() override;

  // Finishes the RPC call with a status.
  void FinishWithStatus(const grpc::Status& status);
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_GET_COMPONENT_AUCTION_CIPHERTEXTS_REACTOR_H_
