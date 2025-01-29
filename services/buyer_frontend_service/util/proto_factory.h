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

#include "api/bidding_auction_servers.pb.h"
#include "rapidjson/document.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/common/loggers/request_log_context.h"

namespace privacy_sandbox::bidding_auction_servers {

// Bidding signals are set to null when KV lookup fails. This is to maintain
// parity with Chrome.
constexpr absl::string_view kNullBiddingSignalsJson = "null";

struct PriorityVectorConfig {
  bool priority_vector_enabled = false;
  // Priority signals vector supplied by SSP via GetBidsRequest.
  rapidjson::Document& priority_signals;
  // Map of IG name to priority vectors supplied via trustedBiddingSignals.
  const absl::flat_hash_map<std::string, rapidjson::Value>&
      per_ig_priority_vectors;
};

struct PrepareGenerateBidsRequestOptions {
  bool enable_kanon = false;
  bool require_bidding_signals = true;
};

struct PrepareGenerateBidsRequestResult {
  std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest> raw_request;
  double percent_igs_filtered;
};

// Creates Proto Objects on heap required for use by BuyerFrontEnd Service.
// TODO(b/248609427): Benchmark allocations on Arena instead of heap
std::unique_ptr<GetBidsResponse::GetBidsRawResponse> CreateGetBidsRawResponse(
    std::unique_ptr<GenerateBidsResponse::GenerateBidsRawResponse>
        raw_response);

// Creates Bidding Request from GetBidsRawRequest, Bidding Signals.
PrepareGenerateBidsRequestResult PrepareGenerateBidsRequest(
    const GetBidsRequest::GetBidsRawRequest& get_bids_raw_request,
    std::unique_ptr<rapidjson::Value> bidding_signals_obj,
    const size_t signal_size, uint32_t data_version,
    const PriorityVectorConfig& priority_vector_config,
    const PrepareGenerateBidsRequestOptions& options = {});

// Creates a request to generate bid for protected app signals.
std::unique_ptr<GenerateProtectedAppSignalsBidsRequest::
                    GenerateProtectedAppSignalsBidsRawRequest>
CreateGenerateProtectedAppSignalsBidsRawRequest(
    const GetBidsRequest::GetBidsRawRequest& raw_request,
    const bool enable_kanon = false);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_BUYER_FRONTEND_SERVICE_PROTO_FACTORY_H_
