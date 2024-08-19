/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "utils/error.h"

#include <string>

#include "googletest/include/gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

TEST(CreateSingleError, CanCreateSingleError) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  document.AddMember(
      "error",
      CreateSingleError(allocator,
                        {.error_type = Error::MODEL_EXECUTION,
                         .description = "Error during model execution."}),
      allocator);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  document.Accept(writer);

  const std::string expected_json =
      R"({"error":{"error_type":"MODEL_EXECUTION","description":"Error during model execution."}})";
  EXPECT_EQ(expected_json, strbuf.GetString());
}

TEST(CreateBatchErrorString, CanCreateBatchErrorResponseWithModelPath) {
  const std::string expected_json =
      R"({"response":[{"error":{"error_type":"GRPC","description":"GRPC Error."}}]})";
  EXPECT_EQ(expected_json,
            CreateBatchErrorString(
                {.error_type = Error::GRPC, .description = "GRPC Error."}));
}

TEST(CreateBatchErrorString, CanCreateBatchErrorResponseWithoutModelPath) {
  const std::string expected_json =
      R"({"response":[{"model_path":"model1","error":{"error_type":"MODEL_NOT_FOUND","description":"Model not found."}}]})";
  EXPECT_EQ(expected_json,
            CreateBatchErrorString({.error_type = Error::MODEL_NOT_FOUND,
                                    .description = "Model not found."},
                                   "model1"));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
