// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/seller_frontend_service/select_ad_reactor_invalid_client.h"

#include <grpcpp/grpcpp.h>

namespace privacy_sandbox::bidding_auction_servers {

SelectAdReactorInvalidClient::SelectAdReactorInvalidClient(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const SellerFrontEndConfig& config)
    : SelectAdReactor(context, request, response, clients, config),
      client_type_(request->client_type()) {}

void SelectAdReactorInvalidClient::Execute() {
  Finish(grpc::Status(
      grpc::INVALID_ARGUMENT,
      absl::StrCat(kUnsupportedClientType, " (",
                   SelectAdRequest_ClientType_Name(client_type_), ")")));
}

}  // namespace privacy_sandbox::bidding_auction_servers
