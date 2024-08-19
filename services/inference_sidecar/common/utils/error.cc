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

#include "absl/strings/string_view.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

rapidjson::Value CreateSingleError(
    rapidjson::Document::AllocatorType& allocator, Error error) {
  rapidjson::Value error_object(rapidjson::kObjectType);
  error_object.AddMember(
      "error_type",
      rapidjson::Value().SetString(
          rapidjson::StringRef(error.ErrorTypeToString()), allocator),
      allocator);
  if (!error.description.empty()) {
    error_object.AddMember(
        "description",
        rapidjson::Value().SetString(
            rapidjson::StringRef(error.description.data()), allocator),
        allocator);
  }
  return error_object;
}

std::string CreateBatchErrorString(Error error, absl::string_view model_path) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

  rapidjson::Value error_object = CreateSingleError(allocator, error);
  rapidjson::Value response_object(rapidjson::kObjectType);
  if (!model_path.empty()) {
    response_object.AddMember(
        "model_path",
        rapidjson::Value().SetString(rapidjson::StringRef(model_path.data())),
        allocator);
  }
  response_object.AddMember("error", error_object, allocator);

  rapidjson::Value response_array(rapidjson::kArrayType);
  response_array.PushBack(response_object, allocator);
  document.AddMember("response", response_array, allocator);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  document.Accept(writer);

  return strbuf.GetString();
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
