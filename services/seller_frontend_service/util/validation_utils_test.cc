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

#include "services/seller_frontend_service/util/validation_utils.h"

#include <gmock/gmock.h>

#include <string>

#include "gtest/gtest.h"
#include "services/common/test/random.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestSeller[] = "sample-seller";

using ::testing::Contains;

TEST(ValidateEncryptedSelectAdRequest, ValidatesSingleSellerInput) {
  ErrorAccumulator error_accumulator;
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(
          CLIENT_TYPE_BROWSER, kTestSeller,
          /*is_consented_debug=*/true);
  bool output = ValidateEncryptedSelectAdRequest(
      request, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER, kTestSeller,
      error_accumulator);
  ASSERT_TRUE(output);
  const auto& errors_map =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE);
  EXPECT_EQ(errors_map.find(ErrorCode::CLIENT_SIDE), errors_map.end());
}

TEST(ValidateEncryptedSelectAdRequest, AddsErrorsToAccumulatorForSingleSeller) {
  ErrorAccumulator error_accumulator;
  SelectAdRequest empty_request;
  bool output = ValidateEncryptedSelectAdRequest(
      empty_request, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER, kTestSeller,
      error_accumulator);
  ASSERT_FALSE(output);
  const auto& errors =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
          .at(ErrorCode::CLIENT_SIDE);
  EXPECT_THAT(errors, Contains(kEmptyProtectedAuctionCiphertextError));
  EXPECT_THAT(errors, Contains(kEmptySellerSignals));
  EXPECT_THAT(errors, Contains(kEmptyAuctionSignals));
  EXPECT_THAT(errors, Contains(kEmptySeller));
  EXPECT_THAT(errors, Contains(kWrongSellerDomain));
  EXPECT_THAT(errors, Contains(kUnsupportedClientType));
}

TEST(ValidateEncryptedSelectAdRequest, ValidatesDeviceComponentSellerInput) {
  ErrorAccumulator error_accumulator;
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(
          CLIENT_TYPE_BROWSER, kTestSeller,
          /*is_consented_debug=*/true);
  bool output = ValidateEncryptedSelectAdRequest(
      request, AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER,
      kTestSeller, error_accumulator);
  ASSERT_TRUE(output);
  const auto& errors_map =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE);
  EXPECT_EQ(errors_map.find(ErrorCode::CLIENT_SIDE), errors_map.end());
}

TEST(ValidateEncryptedSelectAdRequest,
     AddsErrorsToAccumulatorForDeviceComponent) {
  ErrorAccumulator error_accumulator;
  SelectAdRequest empty_request;
  bool output = ValidateEncryptedSelectAdRequest(
      empty_request, AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER,
      kTestSeller, error_accumulator);
  ASSERT_FALSE(output);
  const auto& errors =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
          .at(ErrorCode::CLIENT_SIDE);
  EXPECT_THAT(errors, Contains(kEmptyProtectedAuctionCiphertextError));
  EXPECT_THAT(errors, Contains(kEmptySellerSignals));
  EXPECT_THAT(errors, Contains(kEmptyAuctionSignals));
  EXPECT_THAT(errors, Contains(kEmptySeller));
  EXPECT_THAT(errors, Contains(kWrongSellerDomain));
  EXPECT_THAT(errors, Contains(kUnsupportedClientType));
}

TEST(ValidateEncryptedSelectAdRequest, ValidatesServerComponentSellerInput) {
  ErrorAccumulator error_accumulator;
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(
          CLIENT_TYPE_BROWSER, kTestSeller,
          /*is_consented_debug=*/true);
  bool output = ValidateEncryptedSelectAdRequest(
      request, AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER,
      kTestSeller, error_accumulator);
  ASSERT_TRUE(output);
  const auto& errors_map =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE);
  EXPECT_EQ(errors_map.find(ErrorCode::CLIENT_SIDE), errors_map.end());
}

TEST(ValidateEncryptedSelectAdRequest,
     AddsErrorsToAccumulatorForServerComponent) {
  ErrorAccumulator error_accumulator;
  SelectAdRequest empty_request;
  bool output = ValidateEncryptedSelectAdRequest(
      empty_request, AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER,
      kTestSeller, error_accumulator);
  ASSERT_FALSE(output);
  const auto& errors =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
          .at(ErrorCode::CLIENT_SIDE);
  EXPECT_THAT(errors, Contains(kEmptyProtectedAuctionCiphertextError));
  EXPECT_THAT(errors, Contains(kEmptySellerSignals));
  EXPECT_THAT(errors, Contains(kEmptyAuctionSignals));
  EXPECT_THAT(errors, Contains(kEmptySeller));
  EXPECT_THAT(errors, Contains(kWrongSellerDomain));
  EXPECT_THAT(errors, Contains(kUnsupportedClientType));
}

TEST(ValidateEncryptedSelectAdRequest,
     AddsErrorsToAccumulatorForServerTopLevel) {
  ErrorAccumulator error_accumulator;
  SelectAdRequest empty_request;
  bool output = ValidateEncryptedSelectAdRequest(
      empty_request, AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER,
      kTestSeller, error_accumulator);
  ASSERT_FALSE(output);
  const auto& errors =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
          .at(ErrorCode::CLIENT_SIDE);
  EXPECT_THAT(errors, Contains(kEmptyProtectedAuctionCiphertextError));
  EXPECT_THAT(errors, Contains(kEmptySellerSignals));
  EXPECT_THAT(errors, Contains(kEmptyAuctionSignals));
  EXPECT_THAT(errors, Contains(kEmptySeller));
  EXPECT_THAT(errors, Contains(kWrongSellerDomain));
  EXPECT_THAT(errors, Contains(kUnsupportedClientType));
  EXPECT_THAT(errors, Contains(kNoComponentAuctionResults));
}

TEST(ValidateEncryptedSelectAdRequest,
     AddsErrorForServerTopLevelForMissingCiphertext) {
  ErrorAccumulator error_accumulator;
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(
          CLIENT_TYPE_BROWSER, kTestSeller,
          /*is_consented_debug=*/true);
  auto* auction_res = request.mutable_component_auction_results()->Add();
  auction_res->set_key_id(MakeARandomString());
  bool output = ValidateEncryptedSelectAdRequest(
      request, AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER, kTestSeller,
      error_accumulator);
  ASSERT_FALSE(output);
  const auto& errors =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
          .at(ErrorCode::CLIENT_SIDE);
  EXPECT_THAT(errors, Contains(kEmptyComponentAuctionResults));
}

TEST(ValidateEncryptedSelectAdRequest,
     AddsErrorForServerTopLevelForMissingKeyId) {
  ErrorAccumulator error_accumulator;
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(
          CLIENT_TYPE_BROWSER, kTestSeller,
          /*is_consented_debug=*/true);
  auto* auction_res = request.mutable_component_auction_results()->Add();
  auction_res->set_auction_result_ciphertext(MakeARandomString());
  bool output = ValidateEncryptedSelectAdRequest(
      request, AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER, kTestSeller,
      error_accumulator);
  ASSERT_FALSE(output);
  const auto& errors =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
          .at(ErrorCode::CLIENT_SIDE);
  EXPECT_THAT(errors, Contains(kEmptyComponentAuctionResults));
}

TEST(ValidateEncryptedSelectAdRequest, ValidatesTopLevelSellerInput) {
  ErrorAccumulator error_accumulator;
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<ProtectedAuctionInput>(
          CLIENT_TYPE_BROWSER, kTestSeller,
          /*is_consented_debug=*/true);
  auto* auction_res = request.mutable_component_auction_results()->Add();
  auction_res->set_auction_result_ciphertext(MakeARandomString());
  auction_res->set_key_id(MakeARandomString());
  bool output = ValidateEncryptedSelectAdRequest(
      request, AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER, kTestSeller,
      error_accumulator);
  ASSERT_TRUE(output);
  const auto& errors_map =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE);
  EXPECT_EQ(errors_map.find(ErrorCode::CLIENT_SIDE), errors_map.end());
}

TEST(ValidateComponentAuctionResultTest, ReturnsNoErrorForChaff) {
  ErrorAccumulator error_accumulator;
  std::string generation_id = MakeARandomString();
  AuctionResult input;
  input.set_is_chaff(true);
  bool output = ValidateComponentAuctionResult(input, generation_id,
                                               kTestSeller, error_accumulator);
  ASSERT_TRUE(output);
  const auto& errors_map =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE);
  EXPECT_EQ(errors_map.find(ErrorCode::CLIENT_SIDE), errors_map.end());
}

TEST(ValidateComponentAuctionResultTest, ValidatesAuctionResult) {
  ErrorAccumulator error_accumulator;
  std::string generation_id = MakeARandomString();
  AuctionResult input =
      MakeARandomComponentAuctionResult(generation_id, kTestSeller);
  bool output = ValidateComponentAuctionResult(input, generation_id,
                                               kTestSeller, error_accumulator);
  ASSERT_TRUE(output);
  const auto& errors_map =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE);
  EXPECT_EQ(errors_map.find(ErrorCode::CLIENT_SIDE), errors_map.end());
}

TEST(ValidateComponentAuctionResultTest, ReportsErrors) {
  ErrorAccumulator error_accumulator;
  std::string generation_id = MakeARandomString();
  AuctionResult input = MakeARandomComponentAuctionResult(MakeARandomString(),
                                                          MakeARandomString());
  input.mutable_auction_params()->clear_component_seller();
  input.set_ad_type(AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD);
  input.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(MakeARandomString());
  bool output = ValidateComponentAuctionResult(input, generation_id,
                                               kTestSeller, error_accumulator);
  ASSERT_FALSE(output);
  const auto& errors =
      error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
          .at(ErrorCode::CLIENT_SIDE);
  EXPECT_THAT(errors, Contains(absl::StrFormat(
                          kErrorInAuctionResult,
                          kMismatchedGenerationIdInAuctionResultError)));
  EXPECT_THAT(errors, Contains(absl::StrFormat(
                          kErrorInAuctionResult,
                          kMismatchedTopLevelSellerInAuctionResultError)));
  EXPECT_THAT(errors, Contains(absl::StrFormat(
                          kErrorInAuctionResult,
                          kEmptyComponentSellerInAuctionResultError)));
  EXPECT_THAT(errors, Contains(absl::StrFormat(
                          kErrorInAuctionResult,
                          kUnsupportedAdTypeInAuctionResultError)));
  EXPECT_THAT(errors, Contains(absl::StrFormat(
                          kErrorInAuctionResult,
                          kTopLevelWinReportingUrlsInAuctionResultError)));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
