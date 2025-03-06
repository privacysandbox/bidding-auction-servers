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

#ifndef SERVICES_COMMON_CLIENTS_BUYER_KEY_VALUE_ASYNC_HTTP_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_BUYER_KEY_VALUE_ASYNC_HTTP_CLIENT_H_

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/clients/async_client.h"
#include "services/common/clients/client_params.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/clients/http_kv_server/util/generate_url.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr std::array<absl::string_view, 1> kMandatoryHeaders{
    {"X-BnA-Client-IP"}};

inline constexpr char kDataVersionResponseHeaderName[] = "Data-Version";

// The data used to build the Buyer KV look url suffix
struct GetBuyerValuesInput {
  // [DSP] List of keys to query values for, under the namespace keys.
  UrlKeysSet keys;

  // [DSP] List of interest group names for which to query values.
  UrlKeysSet interest_group_names;

  // [DSP] The browser sets the hostname of the publisher page to be the value.
  std::string hostname;

  // [DSP] The client type that originated the request. Passed to the key/value
  // service.
  ClientType client_type{CLIENT_TYPE_UNKNOWN};

  // [DSP] Optional ID for experiments conducted by buyer. By spec, valid values
  // are [0, 65535].
  std::string buyer_kv_experiment_group_id;
};

// Response from Buyer Key Value server.
struct GetBuyerValuesOutput {
  // Response JSON string.
  std::string result;
  // Used for instrumentation purposes in upper layers.
  size_t request_size;
  size_t response_size;
  // Optional, indicates version of the KV data.
  uint32_t data_version;
  // By default this should be false
  // However, AdTechs can indicate through a header that is ultimately
  // mapped to this flag, that they don't want to make a downstream v2
  // request and the B&A server should process the returned v1 BYOS response.
  bool is_hybrid_v1_return = false;
};

// This class fetches Key/Value pairs from a Buyer Key/Value Server instance
// using a http client.
class BuyerKeyValueAsyncHttpClient
    : public AsyncClient<GetBuyerValuesInput, GetBuyerValuesOutput> {
 public:
  // Builds Buyer KV Value lookup Request.
  static HTTPRequest BuildBuyerKeyValueRequest(
      absl::string_view kv_server_host_domain, const RequestMetadata& metadata,
      std::unique_ptr<GetBuyerValuesInput> client_input,
      std::vector<std::string> expected_response_headers_to_include = {});

  // HttpFetcherAsync argument must outlive instance.
  // This class uses the http client to fetch KV values in real time.
  // If pre_warm is true, it will send an empty request to the
  // KV client to establish connection and cache connection data with the
  // underlying HTTP server. It's false by default.
  explicit BuyerKeyValueAsyncHttpClient(
      absl::string_view kv_server_base_address,
      std::unique_ptr<HttpFetcherAsync> http_fetcher_async,
      bool pre_warm = false);

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
  std::unique_ptr<HttpFetcherAsync> http_fetcher_async_;
  const std::string kv_server_base_address_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_BUYER_KEY_VALUE_ASYNC_HTTP_CLIENT_H_
