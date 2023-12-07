//   Copyright 2023 Google LLC
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

#ifndef SERVICES_COMMON_CLIENTS_BUYER_AD_RETRIEVAL_ASYNC_HTTP_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_BUYER_AD_RETRIEVAL_ASYNC_HTTP_CLIENT_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "rapidjson/document.h"
#include "services/common/clients/async_client.h"
#include "services/common/clients/client_params.h"
#include "services/common/clients/http/http_fetcher_async.h"

namespace privacy_sandbox::bidding_auction_servers {

// Metadata sent by the client.
struct DeviceMetadata {
  std::string client_ip;
  std::string user_agent;
  std::string accept_language;
};

// Data used to build the body of the ad retrieval lookup request.
struct AdRetrievalInput {
  std::string retrieval_data;
  std::string contextual_signals;
  DeviceMetadata device_metadata;

  absl::StatusOr<std::string> ToJson();
};

// Response from Ad Retrieval server.
struct AdRetrievalOutput {
  // A serialized JSON list containing ads and related metadata information.
  std::string ads;

  // Returns an instance of this struct that is parsed from the input json
  // string.
  static absl::StatusOr<AdRetrievalOutput> FromJson(
      absl::string_view response_json);
};

// This class fetches ads from a Ad Retrieval Key/Value Server instance
// using an http client.
class AdsRetrievalAsyncHttpClient
    : public AsyncClient<AdRetrievalInput, AdRetrievalOutput> {
 public:
  // HttpFetcherAsync argument must outlive instance.
  // This class uses the http client to fetch KV values in real time.
  // If pre_warm is true, it will send an empty request to the
  // KV client to establish connection and cache connection data with the
  // underlying HTTP server. It's false by default.
  explicit AdsRetrievalAsyncHttpClient(
      absl::string_view kv_server_base_address,
      std::unique_ptr<HttpFetcherAsync> http_fetcher_async,
      bool pre_warm = true);

  // Executes the http request to a Key-Value Server asynchronously.
  //
  // keys: the request object to execute (the keys to query).
  // on_done: callback called when the request is finished executing.
  // timeout: a timeout value for the request.
  absl::Status Execute(
      std::unique_ptr<AdRetrievalInput> keys, const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<AdRetrievalOutput>>) &&>
          on_done,
      absl::Duration timeout) const override;

 private:
  std::unique_ptr<HttpFetcherAsync> http_fetcher_async_;
  const std::string kv_server_base_address_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_BUYER_AD_RETRIEVAL_ASYNC_HTTP_CLIENT_H_
