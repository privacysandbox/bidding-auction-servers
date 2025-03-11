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

#ifndef FLEDGE_SERVICES_COMMON_CLIENTS_SELLER_FRONTEND_ASYNC_CLIENT_H_
#define FLEDGE_SERVICES_COMMON_CLIENTS_SELLER_FRONTEND_ASYNC_CLIENT_H_

#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/clients/async_client.h"
#include "services/common/clients/client_params.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::Duration sfe_client_max_timeout =
    absl::Milliseconds(60000);

struct SellerFrontEndServiceClientConfig {
  std::string server_addr;
  bool compression = false;
  bool secure_client = true;
  std::string ca_root_pem = "/etc/ssl/certs/ca-certificates.crt";
};

// This class is an async grpc client for the Fledge Bidding Service.
class SellerFrontEndGrpcClient {
 public:
  explicit SellerFrontEndGrpcClient(
      const SellerFrontEndServiceClientConfig& client_config);
  // Sends an asynchronous request via grpc to the SellerFrontEnd Service.
  absl::Status Execute(
      std::unique_ptr<SelectAdRequest> request, const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<SelectAdResponse>>) &&>
          on_done,
      absl::Duration timeout = sfe_client_max_timeout);
  ~SellerFrontEndGrpcClient();

 private:
  std::unique_ptr<SellerFrontEnd::Stub> stub_;
  absl::Mutex active_calls_mutex_;
  int active_calls_count_ ABSL_GUARDED_BY(active_calls_mutex_);
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_COMMON_CLIENTS_SELLER_FRONTEND_ASYNC_CLIENT_H_
