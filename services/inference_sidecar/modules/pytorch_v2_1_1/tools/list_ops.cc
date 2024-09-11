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

// Lists all registered TensorFlow operations.
// Usage: ./builders/tools/bazel-debian run //tools:list_ops

#include <iostream>
#include <string>

#include <torch/torch.h>

int main() {
  std::cout << "PyTorch Version: " << TORCH_VERSION_MAJOR << "."
            << TORCH_VERSION_MINOR << std::endl;

  const std::vector<std::shared_ptr<torch::jit::Operator>> op_list =
      torch::jit::getAllOperators();

  // An op with the same name can be overloaded with multiple signatures.
  std::set<std::string> op_names;
  std::cout << "Op Name" << std::endl;
  for (const auto& op : op_list) {
    op_names.insert(op->schema().name());
  }

  for (const auto& op_name : op_names) {
    std::cout << op_name << std::endl;
  }

  return 0;
}
