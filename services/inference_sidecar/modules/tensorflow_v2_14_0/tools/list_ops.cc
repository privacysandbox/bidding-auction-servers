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
#include <vector>

#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/platform/init_main.h"
#include "tensorflow/core/public/version.h"

int main(int argc, char** argv) {
  tensorflow::port::InitMain(argv[0], &argc, &argv);

  std::cout << "TensorFlow Version: " << TF_VERSION_STRING << std::endl;
  std::cout << "TensorFlow GraphDef Version: " << TF_GRAPH_DEF_VERSION
            << std::endl;

  std::vector<tensorflow::OpDef> op_list;
  tensorflow::OpRegistry::Global()->GetRegisteredOps(&op_list);

  std::cout << "Ops Name, is_stateful" << std::endl;
  for (const auto& op_def : op_list) {
    std::cout << op_def.name() << ", " << op_def.is_stateful() << std::endl;
  }
  return 0;
}
