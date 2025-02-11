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

#include "services/common/clients/http_kv_server/seller/seller_key_value_async_http_client.h"

#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/clients/http_kv_server/util/generate_url.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_metadata.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

// Builds Seller KV Value lookup https request url.
HTTPRequest SellerKeyValueAsyncHttpClient::BuildSellerKeyValueRequest(
    absl::string_view kv_server_host_domain, const RequestMetadata& metadata,
    std::unique_ptr<GetSellerValuesInput> client_input,
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

  if (!client_input->seller_kv_experiment_group_id.empty()) {
    AddAmpersandIfNotFirstQueryParam(&request.url);
    absl::StrAppend(&request.url, "experimentGroupId=",
                    client_input->seller_kv_experiment_group_id);
  }

  if (!client_input->render_urls.empty()) {
    AddListItemsAsQueryParamsToUrl(&request.url, "renderUrls",
                                   client_input->render_urls, true);
  }
  if (!client_input->ad_component_render_urls.empty()) {
    AddListItemsAsQueryParamsToUrl(&request.url, "adComponentRenderUrls",
                                   client_input->ad_component_render_urls,
                                   true);
  }
  request.headers = RequestMetadataToHttpHeaders(metadata);
  request.include_headers = std::move(expected_response_headers_to_include);
  return request;
}

absl::Status SellerKeyValueAsyncHttpClient::Execute(
    std::unique_ptr<GetSellerValuesInput> keys, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
        on_done,
    absl::Duration timeout, RequestContext context) const {
  HTTPRequest request = BuildSellerKeyValueRequest(
      kv_server_base_address_, metadata, std::move(keys),
      {kDataVersionResponseHeaderName});
  PS_VLOG(kKVLog, context.log)
      << "SellerKeyValueAsyncHttpClient Request: " << request.url;
  PS_VLOG(kKVLog, context.log) << "\nSellerKeyValueAsyncHttpClient Headers:\n";
  for (const auto& header : request.headers) {
    PS_VLOG(kKVLog, context.log) << header;
  }
  size_t request_size = 0;
  for (absl::string_view header : request.headers) {
    request_size += header.size();
  }
  request_size += request.url.size();
  EventMessage::KvSignal score_signal =
      KvEventMessage(request.url, context.log, request.headers);
  auto done_callback = [on_done = std::move(on_done), request_size,
                        score_signal = std::move(score_signal), context](
                           absl::StatusOr<HTTPResponse> httpResponse) mutable {
    if (httpResponse.ok()) {
      SetKvEventMessage("SellerKeyValueAsyncHttpClient", httpResponse->body,
                        std::move(score_signal), context.log);
      size_t response_size = httpResponse->body.size();
      uint32_t data_version_value = 0;
      if (auto dv_header_it =
              httpResponse->headers.find(kDataVersionResponseHeaderName);
          dv_header_it != httpResponse->headers.end()) {
        if (const absl::StatusOr<std::string>& dv = dv_header_it->second;
            dv.ok()) {
          if (!absl::SimpleAtoi(*dv, &data_version_value)) {
            // Out param will be "left in an unspecified state" if parsing
            // fails, so reset to 0.
            data_version_value = 0;
            PS_VLOG(kNoisyWarn, context.log)
                << "SellerKeyValueAsyncHttpClient received a non-int, "
                   "negative, "
                   "or too-large data version header.";
          }
        }
      }
      std::unique_ptr<GetSellerValuesOutput> resultUPtr =
          std::make_unique<GetSellerValuesOutput>(GetSellerValuesOutput(
              {std::move(httpResponse->body), request_size, response_size,
               data_version_value}));
      std::move(on_done)(std::move(resultUPtr));
    } else {
      PS_VLOG(kNoisyWarn, context.log)
          << "SellerKeyValueAsyncHttpClients Response fail: "
          << httpResponse.status();
      if (server_common::log::PS_VLOG_IS_ON(kKVLog)) {
        context.log.SetEventMessageField(std::move(score_signal));
      }
      std::move(on_done)(httpResponse.status());
    }
  };
  http_fetcher_async_->FetchUrlWithMetadata(
      request, static_cast<int>(absl::ToInt64Milliseconds(timeout)),
      std::move(done_callback));
  return absl::OkStatus();
}

SellerKeyValueAsyncHttpClient::SellerKeyValueAsyncHttpClient(
    absl::string_view kv_server_base_address,
    std::unique_ptr<HttpFetcherAsync> http_fetcher_async, bool pre_warm)
    : http_fetcher_async_(std::move(http_fetcher_async)),
      kv_server_base_address_(kv_server_base_address) {
  if (pre_warm) {
    auto request = std::make_unique<GetSellerValuesInput>();
    auto status = Execute(
        std::move(request), {},
        [](absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>
               seller_kv_output) mutable {
          if (!seller_kv_output.ok()) {
            PS_LOG(ERROR, SystemLogContext())
                << "SellerKeyValueAsyncHttpClient pre-warm returned status:"
                << seller_kv_output.status().message();
          }
        },
        // Longer timeout for first request
        absl::Milliseconds(60000));
    if (!status.ok()) {
      PS_LOG(ERROR, SystemLogContext())
          << "SellerKeyValueAsyncHttpClient pre-warming failed:" << status;
    }
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
