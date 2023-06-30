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

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "services/common/clients/async_grpc/default_async_grpc_client_stub_test.h"
#include "services/common/clients/bidding_server/bidding_async_client.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using Request = GenerateBidsRequest;
using Response = GenerateBidsResponse;
using ServiceThread = MockServerThread<BiddingServiceMock, Request, Response>;

using BiddingImplementationType =
    ::testing::Types<AsyncGrpcClientTypeDefinitions<
        Request, GenerateBidsRequest::GenerateBidsRawRequest, Response,
        GenerateBidsResponse::GenerateBidsRawResponse, ServiceThread,
        BiddingAsyncGrpcClient, BiddingServiceClientConfig>>;

INSTANTIATE_TYPED_TEST_SUITE_P(BiddingAsyncGrpcClientStubTest,
                               AsyncGrpcClientStubTest,
                               BiddingImplementationType);

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
