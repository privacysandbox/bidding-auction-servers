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

#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kFakeKeyResponse[] =
    "{\"renderUrls\":{\"https://tdsf.doubleclick.net/td/adfetch/"
    "gda?adg_id\\u003d132925906942\\u0026cr_id\\u003d656596039390\\u0026cv_"
    "id\\u003d1\\u0026format\\u003d${AD_WIDTH}x${AD_HEIGHT}\":[[[10009,10406,"
    "11541,10413,10081,10128,10004,10282,11284,11541,10418,10679,10009,10406,"
    "10015,10081,10410],[-1,-1]],null,null,null,null,["
    "\"7576090537747835791\"],null,null,[[65085326,298713704,20285794,"
    "56012541,95772391,23732678,48716882,8274223,92728324,320674061,"
    "213448903,86635774,46568855,63968971,77157244,215380158,78777074,"
    "249425596,210069187,39431335,4022309,311226025,265814569,48572042,"
    "4001425,57016164,323541854,246969283,4032342,256012103,146728848]],["
    "\"en\"],null,null,[[\"1922279805320416001\"]]]},"
    "\"adComponentRenderUrls\":{}}";
}  // namespace

absl::Status FakeSellerKeyValueAsyncHttpClient::Execute(
    std::unique_ptr<GetSellerValuesInput> keys, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
        on_done,
    absl::Duration timeout) const {
  HTTPRequest request =
      SellerKeyValueAsyncHttpClient::BuildSellerKeyValueRequest(
          kv_server_base_address_, metadata, std::move(keys));
  VLOG(2) << "FakeSellerKeyValueAsyncHttpClient Request: " << request.url;
  VLOG(2) << "\nFakeSellerKeyValueAsyncHttpClient Headers:\n";
  for (const auto& header : request.headers) {
    VLOG(2) << header;
  }
  // Below are faked
  std::unique_ptr<GetSellerValuesOutput> resultUPtr =
      std::make_unique<GetSellerValuesOutput>(
          GetSellerValuesOutput({kFakeKeyResponse}));
  std::move(on_done)(std::move(resultUPtr));
  VLOG(2) << "E2E testing received hard coded seller kv request";
  return absl::OkStatus();
}

FakeSellerKeyValueAsyncHttpClient::FakeSellerKeyValueAsyncHttpClient(
    absl::string_view kv_server_base_address)
    : kv_server_base_address_(kv_server_base_address) {}

}  // namespace privacy_sandbox::bidding_auction_servers
