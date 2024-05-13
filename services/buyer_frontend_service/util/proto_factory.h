// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_PROTO_FACTORY_H_
#define FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_PROTO_FACTORY_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"

namespace privacy_sandbox::bidding_auction_servers {

// Creates Proto Objects on heap required for use by BuyerFrontEnd Service.
// TODO(b/248609427): Benchmark allocations on Arena instead of heap
std::unique_ptr<GetBidsResponse::GetBidsRawResponse> CreateGetBidsRawResponse(
    std::unique_ptr<GenerateBidsResponse::GenerateBidsRawResponse>
        raw_response);

// Creates Bidding Request from GetBidsRawRequest, Bidding Signals.
std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
CreateGenerateBidsRawRequest(
    const GetBidsRequest::GetBidsRawRequest& get_bid_raw_request,
    const BuyerInput& buyer_input,
    std::unique_ptr<BiddingSignals> bidding_signals,
    const server_common::LogContext& log_context);

// Creates a request to generate bid for protected app signals.
std::unique_ptr<GenerateProtectedAppSignalsBidsRequest::
                    GenerateProtectedAppSignalsBidsRawRequest>
CreateGenerateProtectedAppSignalsBidsRawRequest(
    const GetBidsRequest::GetBidsRawRequest& raw_request);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_PROTO_FACTORY_H_
