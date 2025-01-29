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

#include "validator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "absl/log/absl_log.h"
#include "absl/strings/string_view.h"
#include "tensorflow/cc/client/client_session.h"
#include "tensorflow/cc/saved_model/loader.h"
#include "tensorflow/core/public/session.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kStatefulModelDir = "./benchmark_models/stateful";
constexpr absl::string_view kPctrModelDir = "./benchmark_models/frozen_pctr";
constexpr absl::string_view kPcvrModelDir = "./benchmark_models/frozen_pcvr";
constexpr absl::string_view kEmbeddingModelDir =
    "./benchmark_models/frozen_embedding";

class AssignOpValidator : public TensorFlowGraphValidator {
 public:
  explicit AssignOpValidator(const tensorflow::GraphDef& graph)
      : TensorFlowGraphValidator(graph) {}
  bool IsOpDenylisted(const tensorflow::NodeDef& node) const override {
    if (node.op() == "AssignVariableOp") {
      return true;
    }
    return false;
  }
  bool IsOpAllowlisted(const tensorflow::NodeDef& node) const override {
    // Allows all stateful ops.
    return true;
  }
};

class ValidatorTest : public ::testing::Test {
 protected:
  ValidatorTest() {
    tensorflow::SessionOptions session_options;
    const std::unordered_set<std::string> tags = {"serve"};
    auto model_bundle = std::make_unique<tensorflow::SavedModelBundle>();
    std::ignore = tensorflow::LoadSavedModel(session_options, {},
                                             std::string(kStatefulModelDir),
                                             tags, model_bundle.get());
    graph_def_ = model_bundle->meta_graph_def.graph_def();
  }

  void ReadModelGraph(absl::string_view model_path) {
    tensorflow::SessionOptions session_options;
    const std::unordered_set<std::string> tags = {"serve"};
    auto model_bundle = std::make_unique<tensorflow::SavedModelBundle>();
    std::ignore = tensorflow::LoadSavedModel(
        session_options, {}, std::string(model_path), tags, model_bundle.get());
    graph_def_ = model_bundle->meta_graph_def.graph_def();
  }

  tensorflow::GraphDef graph_def_;
};

TEST_F(ValidatorTest, IsGraphAllowed_StatefulOps) {
  TensorFlowGraphValidator validator(graph_def_);
  EXPECT_FALSE(validator.IsGraphAllowed());
}

TEST_F(ValidatorTest, IsGraphAllowed_AssignOp) {
  AssignOpValidator validator(graph_def_);
  EXPECT_FALSE(validator.IsGraphAllowed());
}

TEST_F(ValidatorTest, IsGraphAllowed_Print) {}

TEST_F(ValidatorTest, PctrModel_IsGraphAllowed_Ok) {
  ReadModelGraph(kPctrModelDir);
  TensorFlowGraphValidator validator(graph_def_);
  EXPECT_TRUE(validator.IsGraphAllowed());
}

TEST_F(ValidatorTest, PcvrModel_IsGraphAllowed_Ok) {
  ReadModelGraph(kPcvrModelDir);
  TensorFlowGraphValidator validator(graph_def_);
  EXPECT_TRUE(validator.IsGraphAllowed());
}

TEST_F(ValidatorTest, EmbeddedModel_IsGraphAllowed_Ok) {
  ReadModelGraph(kEmbeddingModelDir);
  TensorFlowGraphValidator validator(graph_def_);
  EXPECT_TRUE(validator.IsGraphAllowed());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
