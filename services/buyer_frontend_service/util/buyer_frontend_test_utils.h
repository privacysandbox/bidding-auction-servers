//   Copyright 2022 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#ifndef SERVICES_BUYER_FRONTEND_SERVICE_UTIL_BUYER_FRONTEND_TEST_UTILS_H_
#define SERVICES_BUYER_FRONTEND_SERVICE_UTIL_BUYER_FRONTEND_TEST_UTILS_H_

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "services/buyer_frontend_service/providers/bidding_signals_async_provider.h"
#include "services/common/test/mocks.h"

namespace privacy_sandbox::bidding_auction_servers {

struct BiddingProviderMockOptions {
  std::string bidding_signals_value;
  bool repeated_get_allowed = false;
  std::optional<absl::Status> server_error_to_return;
  bool match_any_params_any_times = false;
  bool is_hybrid_v1_return = false;
};

void SetupBiddingProviderMock(
    const MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>& provider,
    const BiddingProviderMockOptions& options = BiddingProviderMockOptions{});

std::unique_ptr<MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>
SetupBiddingProviderMock(
    const BiddingProviderMockOptions& options = BiddingProviderMockOptions{});

void SetupBiddingProviderMockV2(
    KVAsyncClientMock* kv_async_client,
    const kv_server::v2::GetValuesResponse& response);

void SetupBiddingProviderMockHybrid(
    KVAsyncClientMock* kv_async_client,
    const kv_server::v2::GetValuesResponse& response,
    const std::string& byos_output);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_FRONTEND_SERVICE_UTIL_BUYER_FRONTEND_TEST_UTILS_H_
