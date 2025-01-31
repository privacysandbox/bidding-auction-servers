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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_MOCK_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_MOCK_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "services/common/metric/server_definition.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class KAnonCacheManagerMock : public KAnonCacheManagerInterface {
 public:
  explicit KAnonCacheManagerMock(
      server_common::Executor* executor,
      std::unique_ptr<KAnonGrpcClientInterface> k_anon_client,
      KAnonCacheManagerConfig config = {})
      : KAnonCacheManagerInterface(executor, std::move(k_anon_client), config) {
  }
  ~KAnonCacheManagerMock() override = default;
  MOCK_METHOD(absl::Status, AreKAnonymous,
              (absl::string_view, absl::flat_hash_set<absl::string_view>,
               KAnonCacheManagerInterface::Callback, metric::SfeContext*),
              (override));
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_MOCK_H_
