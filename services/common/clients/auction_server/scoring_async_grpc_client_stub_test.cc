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

#include "gtest/gtest.h"
#include "services/common/clients/async_grpc/default_async_grpc_client_stub_test.h"
#include "services/common/clients/auction_server/scoring_async_client.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using Request = ScoreAdsRequest;
using Response = ScoreAdsResponse;
using ServiceThread = MockServerThread<AuctionServiceMock, Request, Response>;

using ScoringImplementationType =
    ::testing::Types<AsyncGrpcClientTypeDefinitions<
        Request, ScoreAdsRequest::ScoreAdsRawRequest, Response,
        ScoreAdsResponse::ScoreAdsRawResponse, ServiceThread,
        ScoringAsyncGrpcClient, AuctionServiceClientConfig>>;

INSTANTIATE_TYPED_TEST_SUITE_P(ScoringAsyncGrpcClientStubTest,
                               AsyncGrpcClientStubTest,
                               ScoringImplementationType);

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
