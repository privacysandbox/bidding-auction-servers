//  Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_CLIENTS_K_ANON_SERVER_K_ANON_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_K_ANON_SERVER_K_ANON_CLIENT_H_

#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "api/k_anon_query.grpc.pb.h"
#include "api/k_anon_query.pb.h"
#include "services/common/clients/async_client.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::chrome::kanonymityquery::v1::KAnonymousSetsQueryService;
using ::google::chrome::kanonymityquery::v1::ValidateHashesRequest;
using ::google::chrome::kanonymityquery::v1::ValidateHashesResponse;

inline constexpr absl::Duration k_anon_client_max_timeout =
    absl::Milliseconds(60000);

struct KAnonClientConfig {
  std::string server_addr;
  std::string api_key;
  bool compression = false;
  bool secure_client = true;
  std::string ca_root_pem = "/etc/ssl/certs/ca-certificates.crt";
};

class KAnonGrpcClientInterface {
 public:
  KAnonGrpcClientInterface() = default;
  virtual ~KAnonGrpcClientInterface() = default;
  // Sends an asynchronous request via grpc to the k-Anon Service.
  virtual absl::Status Execute(
      std::unique_ptr<ValidateHashesRequest> request,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<ValidateHashesResponse>>) &&>
          on_done,
      absl::Duration timeout = k_anon_client_max_timeout) = 0;
};

class KAnonGrpcClient : public KAnonGrpcClientInterface {
 public:
  explicit KAnonGrpcClient(const KAnonClientConfig& client_config);
  absl::Status Execute(
      std::unique_ptr<ValidateHashesRequest> request,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<ValidateHashesResponse>>) &&>
          on_done,
      absl::Duration timeout = k_anon_client_max_timeout) override;
  ~KAnonGrpcClient() override;

 private:
  std::unique_ptr<KAnonymousSetsQueryService::Stub> stub_;
  absl::flat_hash_map<std::string, std::string> metadata_;
  absl::Mutex active_calls_mutex_;
  int active_calls_count_ ABSL_GUARDED_BY(active_calls_mutex_) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_K_ANON_SERVER_K_ANON_CLIENT_H_
