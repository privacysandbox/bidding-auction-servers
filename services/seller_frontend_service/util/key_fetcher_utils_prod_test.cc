// Copyright 2024 Google LLC
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

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "services/common/public_key_url_allowlist.h"
#include "services/seller_frontend_service/util/key_fetcher_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(KeyFetcherUtilsTest,
     ParseCloudPlatformPublicKeysMap_RejectsUnallowlistedEndpointInProdMode) {
  absl::string_view json = R"json({ "GCP": "fake" })json";
  auto keys_per_cloud = ParseCloudPlatformPublicKeysMap(json);
  ASSERT_TRUE(absl::IsInvalidArgument(keys_per_cloud.status()));
  EXPECT_EQ(keys_per_cloud.status().message(),
            absl::StrCat(kEndpointNotAllowlisted, "fake"));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
