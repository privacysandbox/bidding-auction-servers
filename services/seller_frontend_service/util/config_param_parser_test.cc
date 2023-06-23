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

#include "services/seller_frontend_service/util/config_param_parser.h"

#include <memory>
#include <string>

#include <include/gmock/gmock-actions.h>

#include "services/common/test/mocks.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::Return;

// Leave these escaped; Raw formatting not available in Terraform .tf files.
constexpr absl::string_view kBuyerHostMapForSingleBuyer =
    "{\"https://bid1.com\": \"dns:///bfe-dev.bidding1.com:443\"}";
constexpr absl::string_view kBuyerHostMapForMultipleBuyers =
    "{\"https://bid2.com\": \"dns:///bfe-dev.buyer2-frontend.com:443\", "
    "\"https://bid3.com\": \"dns:///bfe-dev.buyer3-frontend.com:443\", "
    "\"https://bid4.com\": \"dns:///bfe-dev.buyer4-frontend.com:443\", "
    "\"https://bid6.com\": \"dns:///bfe-dev.buyer6-frontend.com:443\", "
    "\"https://bid7.com\": \"dns:///bfe-dev.buyer7-frontend.com:443\"}";

constexpr absl::string_view kBuyerHostMapForLocalhostBuyer =
    "{\"https://bid1.com\": \"localhost:50051\"}";

TEST(StartupParamParserTest, ParseEmptyMap) {
  absl::string_view empty_buyer_host_map = R"json()json";

  auto output = ParseIgOwnerToBfeDomainMap(empty_buyer_host_map);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(StartupParamParserTest, ParseMapWithNoEntries) {
  absl::string_view buyer_host_map_with_no_entries = R"json({})json";

  auto output = ParseIgOwnerToBfeDomainMap(buyer_host_map_with_no_entries);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(StartupParamParserTest, ParseMalformedMapMissingQuote) {
  absl::string_view buyer_host_map_with_missing_quote = R"json({
      "https://example-IG-owner.com": "https://url_with_no_quote_to_end_string.com,
    })json";

  auto output = ParseIgOwnerToBfeDomainMap(buyer_host_map_with_missing_quote);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(StartupParamParserTest, ParseMalformedMapDueToTrailingComma) {
  absl::string_view buyer_host_map_with_trailing_comma = R"json({
      "https://example-IG-owner.com": "https://example-BFE-LB-URL.com",
    })json";

  auto output = ParseIgOwnerToBfeDomainMap(buyer_host_map_with_trailing_comma);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(StartupParamParserTest, HalfCorrectMapStillFails) {
  absl::string_view half_correct_buyer_host_map = R"json({
      "https://example-IG-owner.com": "https://example-BFE-LB-URL.com",
      "https://bidding1.com": 3
    })json";

  auto output = ParseIgOwnerToBfeDomainMap(half_correct_buyer_host_map);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(StartupParamParserTest, ParseSingleBuyerMap) {
  auto output = ParseIgOwnerToBfeDomainMap(kBuyerHostMapForSingleBuyer);
  ASSERT_TRUE(output.ok());
  auto it = output.value().find("https://bid1.com");
  EXPECT_NE(it, output.value().end());
  if (it != output.value().end()) {
    EXPECT_EQ(it->second, "dns:///bfe-dev.bidding1.com:443");
  }

  BuyerFrontEndAsyncClientFactory class_under_test(
      output.value(), nullptr, nullptr, BuyerServiceClientConfig());

  std::shared_ptr<const BuyerFrontEndAsyncClient> actual_buyer_async_client_1 =
      class_under_test.Get("https://bid1.com");
  EXPECT_NE(actual_buyer_async_client_1.get(), nullptr);
}

TEST(StartupParamParserTest, ParseSingleLocalhostBuyerMap) {
  auto output = ParseIgOwnerToBfeDomainMap(kBuyerHostMapForLocalhostBuyer);
  ASSERT_TRUE(output.ok());
  auto it = output.value().find("https://bid1.com");
  EXPECT_NE(it, output.value().end());
  if (it != output.value().end()) {
    EXPECT_EQ(it->second, "localhost:50051");
  }

  BuyerFrontEndAsyncClientFactory class_under_test(
      output.value(), nullptr, nullptr, BuyerServiceClientConfig());

  std::shared_ptr<const BuyerFrontEndAsyncClient> actual_buyer_async_client_1 =
      class_under_test.Get("https://bid1.com");
  EXPECT_NE(actual_buyer_async_client_1.get(), nullptr);
}

TEST(StartupParamParserTest, ParseMultiBuyerMap) {
  auto output = ParseIgOwnerToBfeDomainMap(kBuyerHostMapForMultipleBuyers);
  ASSERT_TRUE(output.ok());
  if (output.ok()) {
    BuyerFrontEndAsyncClientFactory class_under_test(
        output.value(), nullptr, nullptr, BuyerServiceClientConfig());

    std::shared_ptr<const BuyerFrontEndAsyncClient> actual_buyer_async_client;
    std::string current_ig_owner;
    for (int buyer_number : {2, 3, 4, 6, 7}) {
      current_ig_owner =
          absl::StrCat("https://bid", std::to_string(buyer_number), ".com");
      auto it = output.value().find(current_ig_owner);
      EXPECT_NE(it, output.value().end());
      if (it != output.value().end()) {
        EXPECT_EQ(it->second, absl::StrCat("dns:///bfe-dev.buyer",
                                           std::to_string(buyer_number),
                                           "-frontend.com:443"));
        actual_buyer_async_client = class_under_test.Get(current_ig_owner);
        EXPECT_NE(actual_buyer_async_client.get(), nullptr);
      }
    }
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
