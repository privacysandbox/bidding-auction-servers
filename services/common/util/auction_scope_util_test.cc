//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/common/util/auction_scope_util.h"

#include "include/gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
TEST(AuctionScopeUtil,
     ReturnsDeviceComponentForTopLevelSellerInSelectAdRequest) {
  SelectAdRequest request;
  request.mutable_auction_config()->set_top_level_seller(MakeARandomString());
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsDeviceComponentForTopLevelSellerInScoreAdsRequest) {
  ScoreAdsRequest::ScoreAdsRawRequest request;
  request.set_top_level_seller(MakeARandomString());
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsDeviceComponentForNoCloudPlatformInSelectAdRequest) {
  SelectAdRequest request;
  request.mutable_auction_config()->set_top_level_seller(MakeARandomString());
  request.mutable_auction_config()->set_top_level_cloud_platform(
      EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_UNSPECIFIED);
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsServerComponentForGcpCloudPlatformInSelectAdRequest) {
  SelectAdRequest request;
  request.mutable_auction_config()->set_top_level_seller(MakeARandomString());
  request.mutable_auction_config()->set_top_level_cloud_platform(
      EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_GCP);
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsServerComponentForAwsCloudPlatformInSelectAdRequest) {
  SelectAdRequest request;
  request.mutable_auction_config()->set_top_level_seller(MakeARandomString());
  request.mutable_auction_config()->set_top_level_cloud_platform(
      EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_AWS);
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsSingleSellerAuctionForNoTopLevelSellerInScoreAdsRequest) {
  ScoreAdsRequest::ScoreAdsRawRequest request;
  request.clear_top_level_seller();
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsSingleSellerForNoTopSellerAndCloudPlatformInSelectAdRequest) {
  SelectAdRequest request;
  request.mutable_auction_config()->clear_top_level_seller();
  request.mutable_auction_config()->set_top_level_cloud_platform(
      EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_AWS);
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsSingleSellerAuctionForNoTopSellerInSelectAdRequest) {
  SelectAdRequest request;
  request.mutable_auction_config()->clear_top_level_seller();
  request.mutable_auction_config()->set_top_level_cloud_platform(
      EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_AWS);
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsTopLevelAuctionForComponentAuctionResultsInSelectAdRequest) {
  SelectAdRequest request;
  request.mutable_component_auction_results()->Add();
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER);
}

TEST(AuctionScopeUtil,
     ReturnsTopLevelAuctionForComponentAuctionResultsInScoreAdsRequest) {
  ScoreAdsRequest::ScoreAdsRawRequest request;
  request.mutable_component_auction_results()->Add();
  AuctionScope output = GetAuctionScope(request);
  EXPECT_EQ(output, AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER);
}

TEST(AuctionScopeUtil, IsComponentAuction) {
  EXPECT_TRUE(IsComponentAuction(
      AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER));
  EXPECT_TRUE(IsComponentAuction(
      AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER));
  EXPECT_FALSE(
      IsComponentAuction(AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER));
  EXPECT_FALSE(IsComponentAuction(AuctionScope::AUCTION_SCOPE_SINGLE_SELLER));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
