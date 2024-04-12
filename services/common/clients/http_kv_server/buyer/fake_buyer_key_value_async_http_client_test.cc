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

#include "services/common/clients/http_kv_server/buyer/fake_buyer_key_value_async_http_client.h"

#include <fstream>

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::btree_map<std::string, std::string> RequestToPath() {
  return {
      {"interestGroupNames=1j386134098",
       "testing/functional/suts/basic/data/buyer-kv-server/response.json"},
  };
}

TEST(FakeBuyerKeyValueAsyncHttpClient, match_request) {
  auto buyer_input = std::make_unique<GetBuyerValuesInput>();
  absl::btree_set<absl::string_view> interest_group = {"1j386134098"};
  buyer_input->interest_group_names = interest_group;
  buyer_input->buyer_kv_experiment_group_id = "100";

  FakeBuyerKeyValueAsyncHttpClient client("not used", RequestToPath());
}

TEST(FakeBuyerKeyValueAsyncHttpClient, not_match_request) {
  auto buyer_input = std::make_unique<GetBuyerValuesInput>();
  absl::btree_set<absl::string_view> interest_group = {"not_match"};
  buyer_input->interest_group_names = interest_group;

  FakeBuyerKeyValueAsyncHttpClient client("not used", RequestToPath());
}

}  // namespace privacy_sandbox::bidding_auction_servers
