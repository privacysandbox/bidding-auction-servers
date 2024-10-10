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

#include "model/validator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

class MockValidator : public GraphValidatorInterface<int> {
 public:
  std::vector<int> GetAllNodes() const override { return {1, 2, 3, 4}; }

  MOCK_METHOD(bool, IsOpDenylisted, (const int& node), (const override));
  MOCK_METHOD(bool, IsOpStateful, (const int& node), (const override));
  MOCK_METHOD(bool, IsOpAllowlisted, (const int& node), (const override));
};

TEST(ValidatorTest, IsGraphAllowed_Ok) {
  MockValidator validator;
  EXPECT_CALL(validator, IsOpDenylisted(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(validator, IsOpStateful(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_TRUE(validator.IsGraphAllowed());
}

TEST(ValidatorTest, IsGraphAllowed_DeniedOp) {
  MockValidator validator;
  EXPECT_CALL(validator, IsOpDenylisted(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(validator, IsOpDenylisted(4)).WillOnce(testing::Return(true));
  EXPECT_CALL(validator, IsOpStateful(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_FALSE(validator.IsGraphAllowed());
}

TEST(ValidatorTest, IsGraphAllowed_StatefulOp) {
  MockValidator validator;
  EXPECT_CALL(validator, IsOpDenylisted(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(validator, IsOpStateful(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(validator, IsOpStateful(4)).WillOnce(testing::Return(true));
  EXPECT_CALL(validator, IsOpAllowlisted(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_FALSE(validator.IsGraphAllowed());
}

TEST(ValidatorTest, IsGraphAllowed_AllowedStatefulOp) {
  MockValidator validator;
  EXPECT_CALL(validator, IsOpDenylisted(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(validator, IsOpStateful(testing::_))
      .WillRepeatedly(testing::Return(false));
  EXPECT_CALL(validator, IsOpStateful(4)).WillOnce(testing::Return(true));
  EXPECT_CALL(validator, IsOpAllowlisted(testing::_))
      .WillRepeatedly(testing::Return(true));
  EXPECT_TRUE(validator.IsGraphAllowed());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
