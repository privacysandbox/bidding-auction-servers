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
#include "tensorflow_proto_parser.h"

#include <algorithm>
#include <utility>

#include "absl/status/statusor.h"
#include "googletest/include/gtest/gtest.h"
#include "proto/inference_payload.pb.h"
#include "tensorflow/core/framework/tensor_types.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

TEST(TensorflowProtoParserTest, TestConversionToTensor) {
  TensorProto tensor;
  tensor.set_data_type(DataTypeProto::FLOAT);
  tensor.add_tensor_shape(2);
  tensor.add_tensor_shape(3);
  TensorContent* content = tensor.mutable_tensor_content();
  std::vector<float> data = {1.2f, -2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  content->mutable_tensor_content_float()->Resize(data.size(), 0.0f);
  std::copy(data.begin(), data.end(),
            content->mutable_tensor_content_float()->begin());
  // Call the conversion function
  const absl::StatusOr<tensorflow::Tensor> result =
      ConvertProtoToTensor(tensor);
  ASSERT_TRUE(result.ok());
  const tensorflow::Tensor tf_tensor = *std::move(result);
  // Check the dimensions of the resulting tensor
  tensorflow::TensorShape tensorShape = tf_tensor.shape();
  EXPECT_EQ(tensorShape.dims(), 2);
  EXPECT_EQ(tensorShape.dim_size(0), 2);
  EXPECT_EQ(tensorShape.dim_size(1), 3);
  // Check the values in the tensor
  for (int i = 0; i < data.size(); i++) {
    EXPECT_FLOAT_EQ(tf_tensor.flat<float>()(i),
                    tensor.tensor_content().tensor_content_float(i));
  }
}

TEST(TensorflowProtoParserTest, TestConversionToProto) {
  tensorflow::TensorShape tensorShape = {2, 3};
  tensorflow::Tensor tf_tensor(tensorflow::DT_FLOAT, tensorShape);
  std::vector<float> data = {1.2f, -2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  float* tensor_data = tf_tensor.flat<float>().data();
  for (size_t i = 0; i < data.size(); ++i) {
    tensor_data[i] = data[i];
  }
  // Call the conversion function
  const absl::StatusOr<TensorProto> result =
      ConvertTensorToProto("tensor_name", tf_tensor);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().DebugString(),
            "tensor_shape: 2\ntensor_shape: 3\ntensor_name: "
            "\"tensor_name\"\ntensor_content {\n  tensor_content_float: 1.2\n  "
            "tensor_content_float: -2\n  tensor_content_float: 3\n  "
            "tensor_content_float: 4\n  tensor_content_float: 5\n  "
            "tensor_content_float: 6\n}\n");
  EXPECT_EQ(result.value().data_type(), DataTypeProto::FLOAT);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
