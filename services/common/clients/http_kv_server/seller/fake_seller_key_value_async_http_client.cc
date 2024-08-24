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

#include "services/common/clients/http_kv_server/seller/fake_seller_key_value_async_http_client.h"

#include <fstream>

#include "absl/strings/match.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

std::string ReadFromFile(absl::string_view f_path) {
  std::string file_content;
  std::ifstream inputFile(f_path.data());
  if (inputFile.is_open()) {
    file_content.assign((std::istreambuf_iterator<char>(inputFile)),
                        (std::istreambuf_iterator<char>()));
    inputFile.close();
  } else {
    ABSL_LOG(ERROR) << "Failed to open the file.";
  }
  return file_content;
}

absl::btree_map<std::string, std::string> RequestToPath() {
  return {
      // Empty
  };
}

}  // namespace

absl::Status FakeSellerKeyValueAsyncHttpClient::Execute(
    std::unique_ptr<GetSellerValuesInput> keys, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
        on_done,
    absl::Duration timeout, RequestContext context) const {
  HTTPRequest request =
      SellerKeyValueAsyncHttpClient::BuildSellerKeyValueRequest(
          kv_server_base_address_, metadata, std::move(keys));
  PS_VLOG(kKVLog) << "FakeSellerKeyValueAsyncHttpClient Request: "
                  << request.url;
  PS_VLOG(kKVLog) << "\nFakeSellerKeyValueAsyncHttpClient Headers:\n";
  for (const auto& header : request.headers) {
    PS_VLOG(kKVLog) << header;
  }
  // Below are faked

  std::string kv_response;
  for (const auto& [kv_request, kv_signal] : kv_data_) {
    if (absl::StrContains(request.url, kv_request)) {
      kv_response = kv_signal;
    }
  }

  std::unique_ptr<GetSellerValuesOutput> resultUPtr =
      std::make_unique<GetSellerValuesOutput>(
          GetSellerValuesOutput{std::move(kv_response), 0, 0});
  std::move(on_done)(std::move(resultUPtr));
  PS_VLOG(kKVLog) << "E2E testing received hard coded seller kv request";
  return absl::OkStatus();
}

FakeSellerKeyValueAsyncHttpClient::FakeSellerKeyValueAsyncHttpClient(
    absl::string_view kv_server_base_address,
    absl::btree_map<std::string, std::string> request_to_path)
    : kv_server_base_address_(kv_server_base_address) {
  if (request_to_path.empty()) {
    request_to_path = RequestToPath();
  }
  for (const auto& [request, path] : request_to_path) {
    kv_data_[request] = ReadFromFile(path);
  }
}
}  // namespace privacy_sandbox::bidding_auction_servers
