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

#ifndef SERVICES_BUYER_FRONTEND_SERVICE_DATA_BIDDING_SIGNALS_H_
#define SERVICES_BUYER_FRONTEND_SERVICE_DATA_BIDDING_SIGNALS_H_

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
// Buyer FrontEnd Service sends different kinds of bidding signals
// to Bidding Server during the bidding process. This data structure
// adds an interface for the information that needs to be externally
// sourced such as from a gRPC Key Value Service.
struct BiddingSignals {
  std::unique_ptr<std::string> trusted_signals;
  uint32_t data_version;
  // By default this should be false
  // However, AdTechs can indicate through a header that is ultimately
  // mapped to this flag, that they don't want to make a downstream v2
  // request and the B&A server should process the returned v1 BYOS response.
  bool is_hybrid_v1_return = false;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_FRONTEND_SERVICE_DATA_BIDDING_SIGNALS_H_
