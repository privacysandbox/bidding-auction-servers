//  Copyright 2025 Google LLC
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

#include "services/seller_frontend_service/util/chaffing_utils.h"

#include <gmock/gmock.h>

#include "gtest/gtest.h"
#include "services/common/chaffing/mock_moving_median_manager.h"
#include "services/common/random/mock_rng.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

RequestConfig CreateRequestConfig(bool is_chaff, size_t minimum_request_size) {
  return {
      .minimum_request_size = minimum_request_size,
      .compression_type = CompressionType::kGzip,
      .is_chaff_request = is_chaff,
  };
}

class GetChaffingV1GetBidsRequestConfigTest : public ::testing::Test {
 public:
  GetChaffingV1GetBidsRequestConfigTest() : rng_(1) {}

 protected:
  RandomNumberGenerator rng_;
};

TEST_F(GetChaffingV1GetBidsRequestConfigTest, NotChaffRequest) {
  bool is_chaff_request = false;
  RequestConfig actual =
      GetChaffingV1GetBidsRequestConfig(is_chaff_request, rng_);
  EXPECT_EQ(actual, CreateRequestConfig(is_chaff_request, 0));
}

TEST_F(GetChaffingV1GetBidsRequestConfigTest, IsChaffRequest) {
  bool is_chaff_request = true;
  RequestConfig actual =
      GetChaffingV1GetBidsRequestConfig(is_chaff_request, rng_);
  EXPECT_TRUE(actual.is_chaff_request);
  EXPECT_EQ(actual.compression_type, CompressionType::kGzip);
  EXPECT_GE(actual.minimum_request_size, kMinChaffRequestSizeBytes);
  EXPECT_LT(actual.minimum_request_size, kMaxChaffRequestSizeBytes);
}

class GetChaffingV2GetBidsRequestConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ::testing::Mock::VerifyAndClearExpectations(&mock_manager_);
  }

  MockMovingMedianManager mock_manager_;
  RequestContext context_ = NoOpContext();
  absl::string_view test_buyer_name_ = "test_buyer";
};

TEST_F(GetChaffingV2GetBidsRequestConfigTest,
       ChaffRequest_WindowFilled_MedianOk) {
  MockMovingMedianManager mock_manager;
  EXPECT_CALL(mock_manager, IsWindowFilled(test_buyer_name_))
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(mock_manager, GetMedian(test_buyer_name_))
      .WillOnce(::testing::Return(12345));

  bool is_chaff_request = true;
  RequestConfig actual_config = GetChaffingV2GetBidsRequestConfig(
      test_buyer_name_, is_chaff_request, mock_manager, context_);

  RequestConfig expected_config = CreateRequestConfig(is_chaff_request, 12345);
  EXPECT_EQ(actual_config, expected_config);
}

TEST_F(GetChaffingV2GetBidsRequestConfigTest, ChaffRequest_WindowNotFilled) {
  EXPECT_CALL(mock_manager_, IsWindowFilled(test_buyer_name_))
      .WillOnce(::testing::Return(false));

  bool is_chaff_request = true;
  RequestConfig actual_config = GetChaffingV2GetBidsRequestConfig(
      test_buyer_name_, is_chaff_request, mock_manager_, context_);

  RequestConfig expected_config = CreateRequestConfig(
      is_chaff_request, kChaffingV2UnfilledWindowRequestSizeBytes);
  EXPECT_EQ(actual_config, expected_config);
}

TEST_F(GetChaffingV2GetBidsRequestConfigTest,
       ChaffRequest_IsWindowFilledError) {
  EXPECT_CALL(mock_manager_, IsWindowFilled(test_buyer_name_))
      .WillOnce(::testing::Return(absl::InternalError("Window Error")));

  bool is_chaff_request = true;
  RequestConfig actual_config = GetChaffingV2GetBidsRequestConfig(
      test_buyer_name_, is_chaff_request, mock_manager_, context_);

  RequestConfig expected_config = CreateRequestConfig(
      is_chaff_request, kChaffingV2UnfilledWindowRequestSizeBytes);
  EXPECT_EQ(actual_config, expected_config);
}

TEST_F(GetChaffingV2GetBidsRequestConfigTest, ChaffRequest_GetMedianError) {
  EXPECT_CALL(mock_manager_, IsWindowFilled(test_buyer_name_))
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(mock_manager_, GetMedian(test_buyer_name_))
      .WillOnce(::testing::Return(absl::InvalidArgumentError("Median Error")));

  bool is_chaff_request = true;
  RequestConfig actual_config = GetChaffingV2GetBidsRequestConfig(
      test_buyer_name_, is_chaff_request, mock_manager_, context_);

  RequestConfig expected_config = CreateRequestConfig(
      is_chaff_request, kChaffingV2UnfilledWindowRequestSizeBytes);
  EXPECT_EQ(actual_config, expected_config);
}

TEST_F(GetChaffingV2GetBidsRequestConfigTest, NotChaffRequest_WindowFilled) {
  EXPECT_CALL(mock_manager_, IsWindowFilled(test_buyer_name_))
      .WillOnce(::testing::Return(true));

  bool is_chaff_request = false;
  RequestConfig actual_config = GetChaffingV2GetBidsRequestConfig(
      test_buyer_name_, is_chaff_request, mock_manager_, context_);

  EXPECT_EQ(actual_config, CreateRequestConfig(is_chaff_request, 0));
}

TEST_F(GetChaffingV2GetBidsRequestConfigTest, NotChaffRequest_WindowNotFilled) {
  EXPECT_CALL(mock_manager_, IsWindowFilled(test_buyer_name_))
      .WillOnce(::testing::Return(false));

  bool is_chaff_request = false;
  RequestConfig actual_config = GetChaffingV2GetBidsRequestConfig(
      test_buyer_name_, is_chaff_request, mock_manager_, context_);

  RequestConfig expected_config = CreateRequestConfig(
      is_chaff_request, kChaffingV2UnfilledWindowRequestSizeBytes);
  EXPECT_EQ(actual_config, expected_config);
}

TEST_F(GetChaffingV2GetBidsRequestConfigTest,
       NotChaffRequest_IsWindowFilledError) {
  EXPECT_CALL(mock_manager_, IsWindowFilled(test_buyer_name_))
      .WillOnce(::testing::Return(absl::InternalError("Window Error")));

  bool is_chaff_request = false;
  RequestConfig actual_config = GetChaffingV2GetBidsRequestConfig(
      test_buyer_name_, is_chaff_request, mock_manager_, context_);

  RequestConfig expected_config = CreateRequestConfig(
      is_chaff_request, kChaffingV2UnfilledWindowRequestSizeBytes);
  EXPECT_EQ(actual_config, expected_config);
}

TEST(ChaffingUtilsTest, ShouldSkipChaffing) {
  MockRandomNumberGenerator mock_rng;
  EXPECT_CALL(mock_rng, GetUniformReal(0, 1))
      .WillOnce(::testing::Return(0))
      .WillOnce(::testing::Return(.99));
  EXPECT_EQ(ShouldSkipChaffing(1, mock_rng), true);
  EXPECT_EQ(ShouldSkipChaffing(1, mock_rng), false);

  EXPECT_CALL(mock_rng, GetUniformReal(0, 1))
      .WillOnce(::testing::Return(0))
      .WillOnce(::testing::Return(.99));
  EXPECT_EQ(ShouldSkipChaffing(6, mock_rng), true);
  EXPECT_EQ(ShouldSkipChaffing(6, mock_rng), false);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
