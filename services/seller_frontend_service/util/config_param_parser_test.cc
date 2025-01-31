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

using ::privacy_sandbox::server_common::CloudPlatform;
using ::testing::Return;

constexpr absl::string_view kBuyerHostMapForSingleBuyer =
    R"json(
    {
      "https://bid1.com": {
        "url": "dns:///bfe-dev.bidding1.com:443",
        "cloudPlatform": "GCP"
      }
    }
    )json";

constexpr absl::string_view kBuyerHostMapForMultipleBuyers =
    R"json(
    {
      "https://bid2.com": {
        "url": "dns:///bfe-dev.buyer2-frontend.com:443",
        "cloudPlatform": "GCP"
      },
      "https://bid3.com": {
        "url": "dns:///bfe-dev.buyer3-frontend.com:443",
        "cloudPlatform": "AWS"
      },
      "https://bid4.com": {
        "url": "dns:///bfe-dev.buyer4-frontend.com:443",
        "cloudPlatform": "GCP"
      },
      "https://bid5.com": {
        "url": "dns:///bfe-dev.buyer5-frontend.com:443",
        "cloudPlatform": "AWS"
      },
      "https://bid6.com": {
        "url": "dns:///bfe-dev.buyer6-frontend.com:443",
        "cloudPlatform": "GCP"
      },
      "https://bid7.com": {
        "url": "dns:///bfe-dev.buyer7-frontend.com:443",
        "cloudPlatform": "AWS"
      }
    }
    )json";

constexpr absl::string_view kBuyerHostMapForLocalhostBuyer =
    R"json(
    {
      "https://bid1.com": {
        "url": "localhost:50051",
        "cloudPlatform": "LOCAL"
      }
    }
    )json";

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
    EXPECT_EQ(it->second.endpoint, "dns:///bfe-dev.bidding1.com:443");
    EXPECT_EQ(it->second.cloud_platform, CloudPlatform::kGcp);
  }

  BuyerFrontEndAsyncClientFactory class_under_test(
      output.value(), nullptr, nullptr, {.ca_root_pem = kTestCaCertPath});

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
    EXPECT_EQ(it->second.endpoint, "localhost:50051");
    EXPECT_EQ(it->second.cloud_platform, CloudPlatform::kLocal);
  }

  BuyerFrontEndAsyncClientFactory class_under_test(
      output.value(), nullptr, nullptr, {.ca_root_pem = kTestCaCertPath});

  std::shared_ptr<const BuyerFrontEndAsyncClient> actual_buyer_async_client_1 =
      class_under_test.Get("https://bid1.com");
  EXPECT_NE(actual_buyer_async_client_1.get(), nullptr);
}

TEST(StartupParamParserTest, ParseMultiBuyerMap) {
  auto output = ParseIgOwnerToBfeDomainMap(kBuyerHostMapForMultipleBuyers);
  ASSERT_TRUE(output.ok());
  if (output.ok()) {
    BuyerFrontEndAsyncClientFactory class_under_test(
        output.value(), nullptr, nullptr, {.ca_root_pem = kTestCaCertPath});

    std::shared_ptr<const BuyerFrontEndAsyncClient> actual_buyer_async_client;
    std::string current_ig_owner;
    for (int buyer_number : {2, 3, 4, 6, 7}) {
      current_ig_owner =
          absl::StrCat("https://bid", std::to_string(buyer_number), ".com");
      auto it = output.value().find(current_ig_owner);
      EXPECT_NE(it, output.value().end());
      if (it != output.value().end()) {
        std::string url =
            absl::StrCat("dns:///bfe-dev.buyer", std::to_string(buyer_number),
                         "-frontend.com:443");
        CloudPlatform platform =
            (buyer_number % 2 == 0) ? CloudPlatform::kGcp : CloudPlatform::kAws;
        EXPECT_EQ(it->second.endpoint, url);
        EXPECT_EQ(it->second.cloud_platform, platform);
        actual_buyer_async_client = class_under_test.Get(current_ig_owner);
        EXPECT_NE(actual_buyer_async_client.get(), nullptr);
      }
    }
  }
}

TEST(StartupParamParserTest, InvalidCloudPlatform) {
  absl::string_view buyer_host_map = R"json(
  {
    "https://bid1.com": {
      "url": "localhost:50051",
      "cloudPlatform": "foo"
    }
  }
  )json";

  auto output = ParseIgOwnerToBfeDomainMap(buyer_host_map);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParseSellerToCloudPlatformInMapTest, ParsesEmptyMap) {
  absl::string_view seller_platform_map = R"json()json";

  auto output = ParseSellerToCloudPlatformInMap(seller_platform_map);
  EXPECT_TRUE(output.ok());
  EXPECT_EQ(output->size(), 0);
}

TEST(ParseSellerToCloudPlatformInMapTest, ParsesMapWithNoEntries) {
  absl::string_view seller_platform_map = R"json({})json";

  auto output = ParseSellerToCloudPlatformInMap(seller_platform_map);
  EXPECT_TRUE(output.ok());
  EXPECT_EQ(output->size(), 0);
}

TEST(ParseSellerToCloudPlatformInMapTest, DoesNotParseMalformedMap) {
  absl::string_view seller_platform_map = R"json({
      "https://example-seller.com": "GCP",
    })json";

  auto output = ParseSellerToCloudPlatformInMap(seller_platform_map);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParseSellerToCloudPlatformInMapTest, DoesNotParseHalfCorrectMap) {
  absl::string_view seller_platform_map = R"json({
      "https://example-seller.com": "GCP",
      "https://bidding1.com": 3
    })json";

  auto output = ParseSellerToCloudPlatformInMap(seller_platform_map);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ParseSellerToCloudPlatformInMapTest, ParsesValidMap) {
  absl::string_view seller_platform_map = R"json({
      "https://example-seller.com": "GCP",
      "https://example-seller2.com": "AWS"
    })json";
  auto output = ParseSellerToCloudPlatformInMap(seller_platform_map);

  ASSERT_TRUE(output.ok());
  auto it = output.value().find("https://example-seller.com");
  EXPECT_NE(it, output.value().end());
  if (it != output.value().end()) {
    EXPECT_EQ(it->second, CloudPlatform::kGcp);
  }

  auto it_2 = output.value().find("https://example-seller2.com");
  EXPECT_NE(it_2, output.value().end());
  if (it_2 != output.value().end()) {
    EXPECT_EQ(it_2->second, CloudPlatform::kAws);
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
