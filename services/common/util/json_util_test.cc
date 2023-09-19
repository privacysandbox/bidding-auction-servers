/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/common/util/json_util.h"

#include "include/gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(ParseJsonString, WorksForValidJsonString) {
  auto test_str = MakeARandomStructJsonString(4);
  absl::StatusOr<rapidjson::Document> output = ParseJsonString(*test_str);
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_TRUE(output.value().IsObject());
}

TEST(ParseJsonString, ReturnsInvalidArgumentForInvalidJsonString) {
  std::string test_str = "{" + MakeARandomString();
  absl::StatusOr<rapidjson::Document> output = ParseJsonString(test_str);
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(SerializeJsonDoc, WorksForValidDocWithSize) {
  std::string key = MakeARandomString();
  std::string value = MakeARandomString();
  std::string expected_output = "{\"" + key + "\":\"" + value + "\"}";

  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value key_v;
  key_v.SetString(key.c_str(), document.GetAllocator());
  rapidjson::Value val_v;
  val_v.SetString(value.c_str(), document.GetAllocator());
  document.AddMember(key_v, val_v.Move(), document.GetAllocator());

  absl::StatusOr<std::shared_ptr<std::string>> output =
      SerializeJsonDoc(document, 20);
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_STREQ(output.value()->c_str(), expected_output.c_str());
}

TEST(SerializeJsonDoc, WorksForValidDoc) {
  std::string key = MakeARandomString();
  std::string value = MakeARandomString();
  std::string expected_output = "{\"" + key + "\":\"" + value + "\"}";

  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value key_v;
  key_v.SetString(key.c_str(), document.GetAllocator());
  rapidjson::Value val_v;
  val_v.SetString(value.c_str(), document.GetAllocator());
  document.AddMember(key_v, val_v, document.GetAllocator());

  absl::StatusOr<std::string> output = SerializeJsonDoc(document);
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_STREQ(output.value().c_str(), expected_output.c_str());
}

TEST(SerializeJsonDoc, GetString_WorksForStringInDocument) {
  std::string json_str = R"json({"key": "value"})json";
  auto document = ParseJsonString(json_str);
  ASSERT_TRUE(document.ok()) << document.status();

  auto actual_value = GetString(*document, "key");
  ASSERT_TRUE(actual_value.ok()) << actual_value.status();
  EXPECT_EQ(*actual_value, "value");
}

TEST(SerializeJsonDoc, GetString_FailOnEmptyString) {
  std::string json_str = R"json({"key": ""})json";
  auto document = ParseJsonString(json_str);
  ASSERT_TRUE(document.ok()) << document.status();

  auto actual_value = GetString(*document, "key");
  EXPECT_FALSE(actual_value.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
