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

#ifndef SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_BUYER_FAKE_BUYER_KEY_VALUE_ASYNC_HTTP_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_BUYER_FAKE_BUYER_KEY_VALUE_ASYNC_HTTP_CLIENT_H_

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "services/common/clients/http_kv_server/buyer/buyer_key_value_async_http_client.h"

namespace privacy_sandbox::bidding_auction_servers {

// This class fetches Key/Value pairs from a Buyer Key/Value Server instance
// using a http client.
class FakeBuyerKeyValueAsyncHttpClient
    : public AsyncClient<GetBuyerValuesInput, GetBuyerValuesOutput> {
 public:
  // HttpFetcherAsync argument must outlive instance.
  // This class uses the http client to fetch KV values in real time.
  // If pre_warm is true, it will send an empty request to the
  // KV client to establish connection and cache connection data with the
  // underlying HTTP server. It's false by default.
  explicit FakeBuyerKeyValueAsyncHttpClient(
      absl::string_view kv_server_base_address,
      absl::btree_map<std::string, std::string> request_to_path = {});

  // Executes the http request to a Key-Value Server asynchronously.
  //
  // keys: the request object to execute (the keys to query).
  // on_done: callback called when the request is finished executing.
  // timeout: a timeout value for the request.
  absl::Status Execute(
      std::unique_ptr<GetBuyerValuesInput> keys,
      const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
          on_done,
      absl::Duration timeout,
      RequestContext context = NoOpContext()) const override;

 private:
  const std::string kv_server_base_address_;
  // map from kv request to response
  absl::btree_map<std::string, std::string> kv_data_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_BUYER_FAKE_BUYER_KEY_VALUE_ASYNC_HTTP_CLIENT_H_
