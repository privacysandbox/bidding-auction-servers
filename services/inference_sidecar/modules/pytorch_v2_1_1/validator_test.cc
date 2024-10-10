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

#include "absl/log/absl_log.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kStatefulModel = "stateful_model";
constexpr absl::string_view kSubmoduleModel = "submodule_model";
constexpr absl::string_view kExternalFunctionModel = "external_function_model";
constexpr absl::string_view kExternalClassModel = "external_class_model";
constexpr absl::string_view kNestedBlockModel = "nested_block_model";
constexpr absl::string_view kSimpleModel = "simple_model";

class SetAttrValidator : public PyTorchGraphValidator {
 public:
  explicit SetAttrValidator(std::shared_ptr<torch::jit::Graph> graph)
      : PyTorchGraphValidator(graph) {}

  bool IsOpStateful(torch::jit::Node* const& node) const override {
    return node->kind() == c10::Symbol::prim("SetAttr");
  }
};

class ValidatorTest : public ::testing::Test {
 protected:
  ValidatorTest() {
    try {
      model_ = std::make_unique<torch::jit::script::Module>(
          torch::jit::load(std::string(kStatefulModel)));
      model_->eval();
    } catch (...) {
      ABSL_LOG(FATAL) << "PyTorch model loading failed.";
    }
    graph_ = model_->get_method("forward").graph();
  }

  std::unique_ptr<torch::jit::script::Module> model_;
  std::shared_ptr<torch::jit::Graph> graph_;
};

TEST_F(ValidatorTest, IsGraphAllowed_Ok) {
  PyTorchGraphValidator validator(graph_);
  EXPECT_TRUE(validator.IsGraphAllowed());
}

TEST_F(ValidatorTest, IsGraphAllowed_SetAttr) {
  SetAttrValidator validator(graph_);
  EXPECT_FALSE(validator.IsGraphAllowed());
}

class ValidatorGraphTraversalTest : public ::testing::Test {
 protected:
  void ReadModelGraph(absl::string_view model_path) {
    try {
      model_ = std::make_unique<torch::jit::script::Module>(
          torch::jit::load(std::string(model_path)));
      model_->eval();
    } catch (...) {
      ABSL_LOG(FATAL) << "PyTorch model loading failed.";
    }

    try {
      model_ = std::make_unique<torch::jit::script::Module>(
          torch::jit::freeze(*model_));
    } catch (...) {
      ABSL_LOG(FATAL) << "PyTorch model freezing failed.";
    }

    if (model_->attributes().size() > 0) {
      ABSL_LOG(FATAL) << "Failed to inline model graph.";
    }

    graph_ = model_->get_method("forward").graph();
  }

  void CreateSubraph() {
    ASSERT_NE(graph_, nullptr);
    graph_->createWithSubgraph(at::prim::FunctionalGraph);
  }

  std::unique_ptr<torch::jit::script::Module> model_;
  std::shared_ptr<torch::jit::Graph> graph_;
};

TEST_F(ValidatorGraphTraversalTest, ValidateSubmodule) {
  ReadModelGraph(kSubmoduleModel);
  PyTorchGraphValidator validator(graph_);
  EXPECT_FALSE(validator.IsGraphAllowed());
}

TEST_F(ValidatorGraphTraversalTest, ValidateSubgraph) {
  ReadModelGraph(kSimpleModel);
  PyTorchGraphValidator validator(graph_);
  CreateSubraph();
  EXPECT_FALSE(validator.IsGraphAllowed());
}

TEST_F(ValidatorGraphTraversalTest, ValidateExternalFunction) {
  ReadModelGraph(kExternalFunctionModel);
  PyTorchGraphValidator validator(graph_);
  EXPECT_FALSE(validator.IsGraphAllowed());
}

TEST_F(ValidatorGraphTraversalTest, ValidateExternalClass) {
  ReadModelGraph(kExternalClassModel);
  PyTorchGraphValidator validator(graph_);
  EXPECT_FALSE(validator.IsGraphAllowed());
}

TEST_F(ValidatorGraphTraversalTest, ValidateNestedBlocks) {
  ReadModelGraph(kNestedBlockModel);
  PyTorchGraphValidator validator(graph_);
  EXPECT_FALSE(validator.IsGraphAllowed());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
