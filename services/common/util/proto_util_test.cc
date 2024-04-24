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

#include "services/common/util/proto_util.h"

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

namespace privacy_sandbox {
namespace bidding_auction_servers {
namespace {

using ::google::protobuf::util::MessageDifferencer;

TEST(JsonStringToValue, ComplainsOnInvalidInput) {
  auto test_value = R"JSON(
      {
        "k1": [
      })JSON";
  auto proto_value = JsonStringToValue(test_value);
  EXPECT_FALSE(proto_value.ok());
}

TEST(JsonStringToValue, ConvertsWellFormedInput) {
  auto test_value = R"JSON(
      {
        "k1": [1],
        "k2": "2"
      })JSON";
  auto proto_value = JsonStringToValue(test_value);
  CHECK_OK(proto_value);
  auto k1_it = proto_value->struct_value().fields().find("k1");
  ASSERT_NE(k1_it, proto_value->struct_value().fields().end());
  EXPECT_EQ(k1_it->second.list_value().values()[0].number_value(), 1);

  auto k2_it = proto_value->struct_value().fields().find("k2");
  ASSERT_NE(k2_it, proto_value->struct_value().fields().end());
  EXPECT_EQ(k2_it->second.string_value(), "2");
}

TEST(JsonStringToValue, ConvertsStringValue) {
  auto test_value = R"JSON("test_string")JSON";
  auto proto_value = JsonStringToValue(test_value);
  CHECK_OK(proto_value);
  EXPECT_EQ(proto_value->string_value(), "test_string");
}

TEST(JsonStringToValue, ConvertsListValue) {
  auto test_value = R"JSON([1, 2])JSON";
  auto proto_value = JsonStringToValue(test_value);
  CHECK_OK(proto_value);
  EXPECT_EQ(proto_value->list_value().values()[0].number_value(), 1);
  EXPECT_EQ(proto_value->list_value().values()[1].number_value(), 2);
}

}  // namespace
}  // namespace bidding_auction_servers
}  // namespace privacy_sandbox
