// Copyright 2025 Google LLC
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

#include "services/seller_frontend_service/util/buyer_input_proto_utils.h"

#include <gmock/gmock.h>

#include <string>

#include <include/gmock/gmock-matchers.h>

#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

std::string GenerateRandomString(int len) {
  return std::to_string(ToUnixNanos(absl::Now()));
}

int64_t GenerateRandomInt64(int64_t min, int64_t max) {
  std::default_random_engine curr_time_generator(ToUnixNanos(absl::Now()));
  return std::uniform_int_distribution<int64_t>(min, max)(curr_time_generator);
}

// Helper function to set random values for all fields in a message.
void SetRandomMessage(const google::protobuf::Descriptor* descriptor,
                      google::protobuf::Message* message) {
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor->field(i);

    if (field->is_repeated()) {
      const int kNumRepeatedElements = 5;
      for (int j = 0; j < kNumRepeatedElements; j++) {
        switch (field->cpp_type()) {
          case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
            message->GetReflection()->AddUInt32(
                message, field, GenerateRandomInt64(INT32_MIN, INT32_MAX));
            break;
          case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
            message->GetReflection()->AddInt64(
                message, field, GenerateRandomInt64(INT64_MIN, INT64_MAX));
            break;
          case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
            message->GetReflection()->AddUInt32(
                message, field, GenerateRandomInt64(0, UINT32_MAX));
            break;
          case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            message->GetReflection()->AddUInt64(
                message, field, GenerateRandomInt64(0, UINT64_MAX));
            break;
          case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
            message->GetReflection()->AddDouble(
                message, field, static_cast<double>(std::rand()) / RAND_MAX);
            break;
          case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
            message->GetReflection()->AddFloat(
                message, field,
                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));
            break;
          case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
            message->GetReflection()->AddBool(message, field,
                                              std::rand() % 2 == 0);
            break;
          case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
            const google::protobuf::EnumDescriptor* enum_desc =
                field->enum_type();
            const int enum_value_count = enum_desc->value_count();
            message->GetReflection()->AddEnum(
                message, field,
                enum_desc->value(std::rand() % enum_value_count));
          } break;
          case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
            message->GetReflection()->AddString(message, field,
                                                GenerateRandomString(10));
            break;
          case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
            SetRandomMessage(
                field->message_type(),
                message->GetReflection()->AddMessage(message, field));
            break;
        }
      }
    } else {
      switch (field->cpp_type()) {
        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
          message->GetReflection()->SetInt32(
              message, field, GenerateRandomInt64(INT32_MIN, INT32_MAX));
          break;
        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
          message->GetReflection()->SetInt64(
              message, field, GenerateRandomInt64(INT64_MIN, INT64_MAX));
          break;
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
          message->GetReflection()->SetUInt32(
              message, field, GenerateRandomInt64(0, UINT32_MAX));
          break;
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
          message->GetReflection()->SetUInt64(
              message, field, GenerateRandomInt64(0, UINT64_MAX));
          break;
        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
          message->GetReflection()->SetDouble(
              message, field, static_cast<double>(std::rand()) / RAND_MAX);
          break;
        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
          message->GetReflection()->SetFloat(
              message, field,
              static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));
          break;
        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
          message->GetReflection()->SetBool(message, field,
                                            std::rand() % 2 == 0);
          break;
        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
          const google::protobuf::EnumDescriptor* enum_desc =
              field->enum_type();
          const int enum_value_count = enum_desc->value_count();
          message->GetReflection()->SetEnum(
              message, field, enum_desc->value(std::rand() % enum_value_count));
        } break;
        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
          message->GetReflection()->SetString(message, field,
                                              GenerateRandomString(10));
          break;
        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
          SetRandomMessage(
              field->message_type(),
              message->GetReflection()->MutableMessage(message, field));
          break;
      }
    }
  }
}

BuyerInput GenerateBuyerInput() {
  BuyerInput buyer_input;
  SetRandomMessage(buyer_input.GetDescriptor(), &buyer_input);

  // Special handling for oneof fields.
  const google::protobuf::OneofDescriptor* oneof_desc =
      BuyerInput::InterestGroup::descriptor()->FindOneofByName("DeviceSignals");

  for (int i = 0; i < buyer_input.interest_groups_size(); ++i) {
    BuyerInput::InterestGroup* interest_group =
        buyer_input.mutable_interest_groups(i);

    const int field_index = std::rand() % oneof_desc->field_count();
    const google::protobuf::FieldDescriptor* field =
        oneof_desc->field(field_index);

    if (field->message_type() == AndroidSignals::descriptor()) {
      google::protobuf::Message* android_signals =
          interest_group->GetReflection()->MutableMessage(interest_group,
                                                          field);
      SetRandomMessage(android_signals->GetDescriptor(), android_signals);
    } else {
      // buyer_input will already have browser_signals populated in
      // SetRandomMessage() since it comes after android_signals numerically in
      // the proto definition, i.e. no need to explicitly set it.
    }
  }

  return buyer_input;
}

TEST(BuyerInputProtoUtilsTest, ToPrevWinsMs) {
  std::string input =
      R"JSON([[123,"ad_render_id_1"],[456,"ad_render_id_2"]])JSON";
  std::string expected =
      R"JSON([[123000,"ad_render_id_1"],[456000,"ad_render_id_2"]])JSON";
  absl::StatusOr<std::string> actual = ToPrevWinsMs(input);
  ASSERT_TRUE(actual.ok());
  EXPECT_EQ(*actual, expected);
}

// This test uses proto reflection to set every field on a BuyerInput proto to
// a random value, and then runs it through ToBuyerInputForBidding() to verify
// every field is mapped between the BuyerInput and BuyerInputForBidding protos.
// This is to protect against the scenario a new field is added in BuyerInput
// and ToBuyerInputForBidding() isn't updated to map the new field when
// converting to BuyerInputForBidding.
TEST(BuyerInputProtoUtilsTest, ToBuyerInputForBidding) {
  BuyerInput buyer_input = GenerateBuyerInput();
  for (auto& ig : *buyer_input.mutable_interest_groups()) {
    if (ig.has_browser_signals()) {
      // GenerateBuyerInput will set prev_wins to a random string, so we
      // override its value to a valid prev_wins JSON.
      std::string input =
          R"JSON([[123,"ad_render_id_1"],[456,"ad_render_id_2"]])JSON";
      ig.mutable_browser_signals()->set_prev_wins(input);
    }
  }

  BuyerInputForBidding buyer_input_for_bidding =
      ToBuyerInputForBidding(buyer_input);
  for (auto& ig : *buyer_input_for_bidding.mutable_interest_groups()) {
    if (ig.has_browser_signals()) {
      // Verify prev_wins is converted to prev_wins_ms, and then clear the field
      // to compare the rest of the fields in both protos below.
      EXPECT_FALSE(ig.browser_signals().prev_wins_ms().empty());
      ig.mutable_browser_signals()->clear_prev_wins_ms();
    }
  }

  EXPECT_EQ(buyer_input.DebugString(), buyer_input_for_bidding.DebugString());
}

BuyerInputForBidding GenerateBuyerInputForBidding() {
  BuyerInputForBidding buyer_input_for_bidding;
  SetRandomMessage(buyer_input_for_bidding.GetDescriptor(),
                   &buyer_input_for_bidding);

  // Special handling for oneof fields.
  const google::protobuf::OneofDescriptor* oneof_desc =
      BuyerInputForBidding::InterestGroupForBidding::descriptor()
          ->FindOneofByName("DeviceSignals");

  for (int i = 0; i < buyer_input_for_bidding.interest_groups_size(); ++i) {
    BuyerInputForBidding::InterestGroupForBidding* interest_group =
        buyer_input_for_bidding.mutable_interest_groups(i);

    const int field_index = std::rand() % oneof_desc->field_count();
    const google::protobuf::FieldDescriptor* field =
        oneof_desc->field(field_index);

    if (field->message_type() == AndroidSignalsForBidding::descriptor()) {
      google::protobuf::Message* android_signals =
          interest_group->GetReflection()->MutableMessage(interest_group,
                                                          field);
      SetRandomMessage(android_signals->GetDescriptor(), android_signals);
    } else {
      // buyer_input_for_bidding will already have browser_signals populated in
      // SetRandomMessage() since it comes after android_signals numerically in
      // the proto definition, i.e. no need to explicitly set it.
    }
  }

  return buyer_input_for_bidding;
}

// This test uses proto reflection to set every field on a BuyerInputForBidding
// proto to a random value, and then runs it through ToBuyerInput() to verify
// every field is mapped between the BuyerInputForBidding and BuyerInput protos.
// This is to protect against the scenario where a new field is added in
// BuyerInputForBidding and ToBuyerInput() isn't updated to map the new field
// when converting to BuyerInput.
TEST(BuyerInputProtoUtilsTest, ToBuyerInput) {
  BuyerInputForBidding buyer_input_for_bidding = GenerateBuyerInputForBidding();
  BuyerInput buyer_input = ToBuyerInput(buyer_input_for_bidding);

  for (auto& ig : *buyer_input_for_bidding.mutable_interest_groups()) {
    if (ig.has_browser_signals()) {
      ig.mutable_browser_signals()->clear_prev_wins_ms();
    }
  }

  for (int i = 0; i < buyer_input_for_bidding.interest_groups_size(); i++) {
    const auto& ig = buyer_input_for_bidding.interest_groups(i);
    ASSERT_EQ(ig.DebugString(), buyer_input.interest_groups(i).DebugString());
  }

  EXPECT_EQ(buyer_input.DebugString(), buyer_input_for_bidding.DebugString());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
