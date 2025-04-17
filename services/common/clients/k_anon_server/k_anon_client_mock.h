//  Copyright 2025 Google LLC
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

#ifndef SERVICES_COMMON_CLIENTS_K_ANON_SERVER_K_ANON_CLIENT_MOCK_H_
#define SERVICES_COMMON_CLIENTS_K_ANON_SERVER_K_ANON_CLIENT_MOCK_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "api/k_anon_query.grpc.pb.h"
#include "api/k_anon_query.pb.h"
#include "gmock/gmock.h"
#include "services/common/clients/k_anon_server/k_anon_client.h"

namespace privacy_sandbox::bidding_auction_servers {

class MockKAnonClient : public KAnonGrpcClientInterface {
 public:
  MockKAnonClient() = default;
  MOCK_METHOD(
      absl::Status, Execute,
      (std::unique_ptr<ValidateHashesRequest> request,
       absl::AnyInvocable<
           void(absl::StatusOr<std::unique_ptr<ValidateHashesResponse>>) &&>
           on_done,
       absl::Duration timeout),
      (override));
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_K_ANON_SERVER_K_ANON_CLIENT_MOCK_H_
