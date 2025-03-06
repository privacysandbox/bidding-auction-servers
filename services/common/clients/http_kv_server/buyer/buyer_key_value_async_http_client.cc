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

#include "services/common/clients/http_kv_server/buyer/buyer_key_value_async_http_client.h"

#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/clients/http_kv_server/util/generate_url.h"
#include "services/common/clients/http_kv_server/util/process_response.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_metadata.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr auto kEnableEncodeParams = true;

}  // namespace

HTTPRequest BuyerKeyValueAsyncHttpClient::BuildBuyerKeyValueRequest(
    absl::string_view kv_server_host_domain, const RequestMetadata& metadata,
    std::unique_ptr<GetBuyerValuesInput> client_input,
    std::vector<std::string> expected_response_headers_to_include) {
  HTTPRequest request;
  ClearAndMakeStartOfUrl(kv_server_host_domain, &request.url);

  // In the future, we will expose the client type param for
  // all client types, but for now we will limit it to android for
  // ease of Key/Value service interoperability with the on-device api.
  if (client_input->client_type == CLIENT_TYPE_ANDROID) {
    AddAmpersandIfNotFirstQueryParam(&request.url);
    absl::StrAppend(&request.url, "client_type=", client_input->client_type);
  }

  if (!(client_input->hostname.empty())) {
    AddAmpersandIfNotFirstQueryParam(&request.url);
    absl::StrAppend(&request.url, "hostname=", client_input->hostname);
  }
  if (!client_input->buyer_kv_experiment_group_id.empty()) {
    AddAmpersandIfNotFirstQueryParam(&request.url);
    absl::StrAppend(&request.url, "experimentGroupId=",
                    client_input->buyer_kv_experiment_group_id);
  }
  if (!client_input->keys.empty()) {
    AddListItemsAsQueryParamsToUrl(&request.url, "keys", client_input->keys,
                                   kEnableEncodeParams);
  }
  if (!client_input->interest_group_names.empty()) {
    AddListItemsAsQueryParamsToUrl(&request.url, "interestGroupNames",
                                   client_input->interest_group_names,
                                   kEnableEncodeParams);
  }

  request.headers = RequestMetadataToHttpHeaders(metadata, kMandatoryHeaders);

  request.include_headers = std::move(expected_response_headers_to_include);

  return request;
}

absl::Status BuyerKeyValueAsyncHttpClient::Execute(
    std::unique_ptr<GetBuyerValuesInput> keys, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
        on_done,
    absl::Duration timeout, RequestContext context) const {
  HTTPRequest request = BuildBuyerKeyValueRequest(
      kv_server_base_address_, metadata, std::move(keys),
      {kDataVersionResponseHeaderName, kHybridV1OnlyResponseHeaderName});

  size_t request_size = 0;
  for (absl::string_view header : request.headers) {
    request_size += header.size();
  }
  request_size += request.url.size();
  EventMessage::KvSignal bid_signal =
      KvEventMessage(request.url, context.log, request.headers);
  auto done_callback = [on_done = std::move(on_done), request_size,
                        bid_signal = std::move(bid_signal), context](
                           absl::StatusOr<HTTPResponse> httpResponse) mutable {
    if (httpResponse.ok()) {
      SetKvEventMessage("BuyerKeyValueAsyncHttpClient", httpResponse->body,
                        std::move(bid_signal), context.log);
      size_t response_size = httpResponse->body.size();
      uint32_t data_version_value = 0;
      if (auto dv_header_it =
              httpResponse->headers.find(kDataVersionResponseHeaderName);
          dv_header_it != httpResponse->headers.end()) {
        if (const auto& dv_statusor = dv_header_it->second; dv_statusor.ok()) {
          if (!absl::SimpleAtoi(dv_statusor.value(), &data_version_value)) {
            // Out param will be "left in an unspecified state" if parsing
            // fails, so reset to 0.
            data_version_value = 0;
            PS_VLOG(kNoisyWarn, context.log)
                << "BuyerKeyValueAsyncHttpClient received a non-int, negative, "
                   "or too-large data version header.";
          }
        }
      }
      bool is_hybrid_v1_return_value = IsHybridV1Return(*httpResponse);
      std::unique_ptr<GetBuyerValuesOutput> resultUPtr =
          std::make_unique<GetBuyerValuesOutput>(GetBuyerValuesOutput(
              {std::move(httpResponse->body), request_size, response_size,
               data_version_value, is_hybrid_v1_return_value}));
      std::move(on_done)(std::move(resultUPtr));
    } else {
      PS_VLOG(kNoisyWarn, context.log)
          << "BuyerKeyValueAsyncHttpClient Failure Response: "
          << httpResponse.status();
      if (server_common::log::PS_VLOG_IS_ON(kKVLog)) {
        context.log.SetEventMessageField(std::move(bid_signal));
      }
      std::move(on_done)(httpResponse.status());
    }
  };
  PS_VLOG(kKVLog, context.log) << "BTS Request Url:\n"
                               << request.url << "\nHeaders:\n";
  for (const auto& header : request.headers) {
    PS_VLOG(kKVLog, context.log) << header;
  }
  http_fetcher_async_->FetchUrlWithMetadata(
      request, static_cast<int>(absl::ToInt64Milliseconds(timeout)),
      std::move(done_callback));
  return absl::OkStatus();
}

BuyerKeyValueAsyncHttpClient::BuyerKeyValueAsyncHttpClient(
    absl::string_view kv_server_base_address,
    std::unique_ptr<HttpFetcherAsync> http_fetcher_async, bool pre_warm)
    : http_fetcher_async_(std::move(http_fetcher_async)),
      kv_server_base_address_(kv_server_base_address) {
  if (pre_warm) {
    auto request = std::make_unique<GetBuyerValuesInput>();
    auto status = Execute(
        std::move(request), {},
        [](absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>
               buyer_kv_output) mutable {
          if (!buyer_kv_output.ok()) {
            PS_LOG(ERROR, SystemLogContext())
                << "BuyerKeyValueAsyncHttpClient pre-warm returned status:"
                << buyer_kv_output.status().message();
          }
        },
        // Longer timeout for first request
        absl::Milliseconds(60000));
    if (!status.ok()) {
      PS_LOG(ERROR, SystemLogContext())
          << "BuyerKeyValueAsyncHttpClient pre-warming failed: " << status;
    }
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
