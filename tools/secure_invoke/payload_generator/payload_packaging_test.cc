// Copyright 2023 Google LLC
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

#include "tools/secure_invoke/payload_generator/payload_packaging.h"

#include <utility>

#include <google/protobuf/util/message_differencer.h>

#include "api/bidding_auction_servers.grpc.pb.h"
#include "include/gmock/gmock.h"
#include "include/gtest/gtest.h"
#include "services/common/test/random.h"
#include "services/common/util/json_util.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "tools/secure_invoke/payload_generator/payload_packaging_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

constexpr char kSellerOriginDomain[] = "seller.com";

rapidjson::Document ProtoToDocument(const google::protobuf::Message& message) {
  std::string json_string;
  auto message_convert_status =
      google::protobuf::util::MessageToJsonString(message, &json_string);
  CHECK(message_convert_status.ok()) << message_convert_status;
  auto doc = ParseJsonString(json_string);
  CHECK(doc.ok()) << doc.status();
  return std::move(*doc);
}

// Creates a valid input json doc.
std::string GetValidInput(const SelectAdRequest& expected_output) {
  rapidjson::Document input;
  input.SetObject();

  rapidjson::Value protected_audience_doc;
  protected_audience_doc.SetObject();
  rapidjson::Value buyerMapValue;
  buyerMapValue.SetObject();
  protected_audience_doc.AddMember(kBuyerInputMapField, buyerMapValue,
                                   input.GetAllocator());

  // Copy auction config from expected output.
  input.AddMember(kAuctionConfigField,
                  ProtoToDocument(expected_output.auction_config()),
                  input.GetAllocator());
  input.AddMember(kProtectedAudienceInputField, protected_audience_doc,
                  input.GetAllocator());

  auto input_str = SerializeJsonDoc(input);
  CHECK(input_str.ok()) << input_str.status();
  return input_str.value();
}

TEST(PaylodPackagingTest, FailsOnEmptyJson) {
  EXPECT_DEATH(PackagePlainTextSelectAdRequestToJson(""), "");
}

TEST(PaylodPackagingTest, FailsOnInvalidJson) {
  EXPECT_DEATH(PackagePlainTextSelectAdRequestToJson("random-string"), "");
}

TEST(PaylodPackagingTest, FailsOnEmptyInputJson) {
  rapidjson::Document input;
  input.SetObject();

  auto input_str = SerializeJsonDoc(input);
  CHECK(input_str.ok()) << input_str.status();
  EXPECT_DEATH(PackagePlainTextSelectAdRequestToJson(input_str.value()), "");
}

TEST(PaylodPackagingTest, FailsOnInputJsonMissingProtectedAudience) {
  rapidjson::Document input;
  input.SetObject();
  rapidjson::Value auction_config;
  auction_config.SetObject();
  input.AddMember(kAuctionConfigField, auction_config, input.GetAllocator());

  auto input_str = SerializeJsonDoc(input);
  CHECK(input_str.ok()) << input_str.status();
  EXPECT_DEATH(PackagePlainTextSelectAdRequestToJson(input_str.value()), "");
}

TEST(PaylodPackagingTest, FailsOnInputJsonMissingBuyerInputMap) {
  rapidjson::Document input;
  input.SetObject();

  rapidjson::Value auction_config;
  auction_config.SetObject();
  input.AddMember(kAuctionConfigField, auction_config, input.GetAllocator());

  rapidjson::Value protected_audience_doc;
  protected_audience_doc.SetObject();
  input.AddMember(kProtectedAudienceInputField, protected_audience_doc,
                  input.GetAllocator());

  auto input_str = SerializeJsonDoc(input);
  CHECK(input_str.ok()) << input_str.status();
  EXPECT_DEATH(PackagePlainTextSelectAdRequestToJson(input_str.value()), "");
}

TEST(PaylodPackagingTest, HandlesOldBuyerInputMapKey) {
  rapidjson::Document input;
  input.SetObject();

  rapidjson::Value auction_config;
  auction_config.SetObject();
  input.AddMember(kAuctionConfigField, auction_config, input.GetAllocator());

  rapidjson::Value protected_audience_doc;
  protected_audience_doc.SetObject();
  rapidjson::Value buyerMapValue;
  buyerMapValue.SetObject();
  protected_audience_doc.AddMember(kOldBuyerInputMapField, buyerMapValue,
                                   input.GetAllocator());
  input.AddMember(kProtectedAudienceInputField, protected_audience_doc,
                  input.GetAllocator());

  auto input_str = SerializeJsonDoc(input);
  CHECK(input_str.ok()) << input_str.status();
  PackagePlainTextSelectAdRequestToJson(input_str.value());
}

TEST(PaylodPackagingTest, FailsOnInputJsonMissingAuctionConfig) {
  rapidjson::Document input;
  input.SetObject();
  rapidjson::Value protected_audience_doc;
  protected_audience_doc.SetObject();
  rapidjson::Value buyerMapValue;
  buyerMapValue.SetObject();
  protected_audience_doc.AddMember(kBuyerInputMapField, buyerMapValue,
                                   input.GetAllocator());
  input.AddMember(kProtectedAudienceInputField, protected_audience_doc,
                  input.GetAllocator());

  auto input_str = SerializeJsonDoc(input);
  CHECK(input_str.ok()) << input_str.status();
  EXPECT_DEATH(PackagePlainTextSelectAdRequestToJson(input_str.value()), "");
}

TEST(PaylodPackagingTest, CopiesAuctionConfigFromSourceJson) {
  ProtectedAudienceInput protected_audience_input =
      MakeARandomProtectedAudienceInput();
  SelectAdRequest expected =
      MakeARandomSelectAdRequest(kSellerOriginDomain, protected_audience_input);
  std::string input = GetValidInput(expected);

  std::string output = PackagePlainTextSelectAdRequestToJson(input);
  SelectAdRequest actual;
  auto parse_output =
      google::protobuf::util::JsonStringToMessage(output, &actual);
  ASSERT_TRUE(parse_output.ok()) << parse_output;

  google::protobuf::util::MessageDifferencer diff;
  std::string difference;
  diff.ReportDifferencesToString(&difference);
  EXPECT_TRUE(diff.Compare(actual.auction_config(), expected.auction_config()))
      << difference;
}

TEST(PaylodPackagingTest, SetsTheCorrectClientType) {
  ProtectedAudienceInput protected_audience_input =
      MakeARandomProtectedAudienceInput();
  SelectAdRequest expected =
      MakeARandomSelectAdRequest(kSellerOriginDomain, protected_audience_input);
  auto input = GetValidInput(expected);

  std::string output = PackagePlainTextSelectAdRequestToJson(input);
  SelectAdRequest actual;
  auto parse_output =
      google::protobuf::util::JsonStringToMessage(output, &actual);
  ASSERT_TRUE(parse_output.ok()) << parse_output;
  EXPECT_EQ(actual.client_type(), SelectAdRequest_ClientType_BROWSER);
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
