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

#include <memory>
#include <string>
#include <unordered_set>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "tensorflow/cc/saved_model/loader.h"
#include "tensorflow/cc/saved_model/tag_constants.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/public/session.h"
#include "tensorflow/core/public/session_options.h"
#include "tensorflow/tsl/platform/env.h"
#include "tensorflow/tsl/platform/file_system.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kModelPath = "test_model";
constexpr absl::string_view kRamFileSystemModelPath = "ram://test_model";
constexpr absl::string_view kModelFiles[] = {
    "saved_model.pb",
    "variables/variables.index",
    "variables/variables.data-00000-of-00001",
};

TEST(RamFileSystemTest, LoadModelFromLocalDisk_Success) {
  tensorflow::SessionOptions session_options;
  const std::unordered_set<std::string> tags = {"serve"};

  auto model_bundle = std::make_unique<tensorflow::SavedModelBundle>();
  auto status = tensorflow::LoadSavedModel(
      session_options, {}, std::string(kModelPath), tags, model_bundle.get());
  EXPECT_TRUE(status.ok());
}

TEST(RamFileSystemTest, LoadModelFromMemory_NotFound) {
  tensorflow::SessionOptions session_options;
  const std::unordered_set<std::string> tags = {"serve"};

  auto model_bundle = std::make_unique<tensorflow::SavedModelBundle>();
  auto status = tensorflow::LoadSavedModel(session_options, {},
                                           std::string(kRamFileSystemModelPath),
                                           tags, model_bundle.get());
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kNotFound);
}

void CopyModelToRamFileSystem() {
  for (const auto file : kModelFiles) {
    const std::string local_file = absl::StrCat(kModelPath, "/", file);
    const std::string memory_file =
        absl::StrCat(kRamFileSystemModelPath, "/", file);
    ASSERT_TRUE(tsl::Env::Default()->CopyFile(local_file, memory_file).ok());
  }
}

TEST(RamFileSystemTest, LoadModelFromMemory_Success) {
  CopyModelToRamFileSystem();

  tensorflow::SessionOptions session_options;
  const std::unordered_set<std::string> tags = {"serve"};

  auto model_bundle = std::make_unique<tensorflow::SavedModelBundle>();
  auto status = tensorflow::LoadSavedModel(session_options, {},
                                           std::string(kRamFileSystemModelPath),
                                           tags, model_bundle.get());
  EXPECT_TRUE(status.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
