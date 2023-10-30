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
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/cpp/communication/encoding_utils.h"
#include "src/cpp/communication/ohttp_utils.h"
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
  auto document = ProtoToDocument(expected_output.auction_config());
  input.AddMember(kAuctionConfigField, document, input.GetAllocator());
  input.AddMember(kProtectedAuctionInputField, protected_audience_doc,
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
  input.AddMember(kProtectedAuctionInputField, protected_audience_doc,
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
  input.AddMember(kProtectedAuctionInputField, protected_audience_doc,
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
  input.AddMember(kProtectedAuctionInputField, protected_audience_doc,
                  input.GetAllocator());

  auto input_str = SerializeJsonDoc(input);
  CHECK(input_str.ok()) << input_str.status();
  EXPECT_DEATH(PackagePlainTextSelectAdRequestToJson(input_str.value()), "");
}

TEST(PaylodPackagingTest, CopiesAuctionConfigFromSourceJson) {
  auto protected_audience_input =
      MakeARandomProtectedAuctionInput<ProtectedAuctionInput>();
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
  auto protected_audience_input =
      MakeARandomProtectedAuctionInput<ProtectedAuctionInput>();
  SelectAdRequest expected =
      MakeARandomSelectAdRequest(kSellerOriginDomain, protected_audience_input);
  auto input = GetValidInput(expected);

  std::string output = PackagePlainTextSelectAdRequestToJson(input);
  SelectAdRequest actual;
  auto parse_output =
      google::protobuf::util::JsonStringToMessage(output, &actual);
  ASSERT_TRUE(parse_output.ok()) << parse_output;
  EXPECT_EQ(actual.client_type(), ClientType::CLIENT_TYPE_BROWSER);
}

TEST(PaylodPackagingTest, SetsTheCorrectDebugReportingFlag) {
  bool enable_debug_reporting = true;
  auto input =
      R"JSON({"auction_config":{"sellerSignals":"{\"seller_signal\": \"1698245045006099572\"}","auctionSignals":"{\"auction_signal\": \"1698245045006100642\"}","buyerList":["1698245045005905922"],"seller":"seller.com","perBuyerConfig":{"1698245045005905922":{"buyerSignals":"1698245045006101412"}},"buyerTimeoutMs":1000},"raw_protected_audience_input":{"raw_buyer_input":{"ad_tech_A.com":{"interestGroups":[{"name":"1698245045006110232","biddingSignalsKeys":["1698245045006110482","1698245045006110862"],"adRenderIds":["ad_render_id_1698245045006122582","ad_render_id_1698245045006123002","ad_render_id_1698245045006123242","ad_render_id_1698245045006123472","ad_render_id_1698245045006123772","ad_render_id_1698245045006124002","ad_render_id_1698245045006124212","ad_render_id_1698245045006124412","ad_render_id_1698245045006124672"],"userBiddingSignals":"{\"1698245045006112422\":\"1698245045006112622\",\"1698245045006113232\":\"1698245045006113422\",\"1698245045006111322\":\"1698245045006111702\",\"1698245045006112802\":\"1698245045006112972\",\"1698245045006112032\":\"1698245045006112252\"}","browserSignals":{"joinCount":"8","bidCount":"41","recency":"1698245045","prevWins":"[[1698245045,\"ad_render_id_1698245045006122582\"],[1698245045,\"ad_render_id_1698245045006123002\"],[1698245045,\"ad_render_id_1698245045006123242\"],[1698245045,\"ad_render_id_1698245045006123472\"],[1698245045,\"ad_render_id_1698245045006123772\"],[1698245045,\"ad_render_id_1698245045006124002\"],[1698245045,\"ad_render_id_1698245045006124212\"],[1698245045,\"ad_render_id_1698245045006124412\"],[1698245045,\"ad_render_id_1698245045006124672\"]]"}}]}}}})JSON";

  auto select_ad_reqcurrent_all_debug_urls_chars =
      std::move(PackagePlainTextSelectAdRequest(input, CLIENT_TYPE_BROWSER,
                                                kDefaultPublicKey, kTestKeyId,
                                                enable_debug_reporting))
          .first;

  // Decrypt.
  server_common::PrivateKey private_key;
  private_key.key_id = std::to_string(kTestKeyId);
  private_key.private_key = GetHpkePrivateKey(kDefaultPrivateKey);
  auto decrypted_response = server_common::DecryptEncapsulatedRequest(
      private_key, select_ad_reqcurrent_all_debug_urls_chars
                       ->protected_auction_ciphertext());
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
  ASSERT_TRUE(decoded_request.ok()) << decoded_request.status();

  // Decode.
  ErrorAccumulator error_accumulator;
  absl::StatusOr<ProtectedAuctionInput> actual = Decode<ProtectedAuctionInput>(
      decoded_request->compressed_data, error_accumulator);
  ASSERT_TRUE(actual.ok()) << actual.status();

  EXPECT_TRUE(actual.value().enable_debug_reporting());
}
}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
