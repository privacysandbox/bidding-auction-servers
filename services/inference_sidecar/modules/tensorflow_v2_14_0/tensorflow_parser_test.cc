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

#include "tensorflow_parser.h"

#include <algorithm>
#include <utility>

#include "absl/status/statusor.h"
#include "googletest/include/gtest/gtest.h"
#include "tensorflow/core/framework/tensor_types.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

TEST(TensorflowParserTest, TestConversion) {
  Tensor tensor;
  tensor.data_type = DataType::kFloat;
  tensor.tensor_content = {"1.2", "-2", "3", "4", "5", "6"};
  tensor.tensor_shape = {2, 3};

  // Call the conversion function
  const absl::StatusOr<tensorflow::Tensor> result =
      ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const tensorflow::Tensor tf_tensor = *std::move(result);

  // Check the dimensions of the resulting tensor
  tensorflow::TensorShape tensorShape = tf_tensor.shape();
  EXPECT_EQ(tensorShape.dims(), 2);
  EXPECT_EQ(tensorShape.dim_size(0), 2);
  EXPECT_EQ(tensorShape.dim_size(1), 3);

  // Check the values in the tensor
  for (int i = 0; i < tensor.tensor_content.size(); i++) {
    EXPECT_FLOAT_EQ(tf_tensor.flat<float>()(i),
                    std::stof(tensor.tensor_content[i]));
  }
}

TEST(TensorflowParserTest, TestConversionInt8) {
  Tensor tensor;
  tensor.data_type = DataType::kInt8;
  tensor.tensor_content = {"5"};
  tensor.tensor_shape = {1, 1};

  const absl::StatusOr<tensorflow::Tensor> result =
      ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const tensorflow::Tensor tf_tensor = *std::move(result);

  tensorflow::DataType data_type = tf_tensor.dtype();
  EXPECT_EQ(data_type, tensorflow::DataType::DT_INT8);

  EXPECT_EQ(tf_tensor.flat<int8_t>()(0), 5);
}

TEST(TensorflowParserTest, TestConversionInt16) {
  Tensor tensor;
  tensor.data_type = DataType::kInt16;
  tensor.tensor_content = {"5"};
  tensor.tensor_shape = {1, 1};

  const absl::StatusOr<tensorflow::Tensor> result =
      ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const tensorflow::Tensor tf_tensor = *std::move(result);

  tensorflow::DataType data_type = tf_tensor.dtype();
  EXPECT_EQ(data_type, tensorflow::DataType::DT_INT16);

  EXPECT_EQ(tf_tensor.flat<short>()(0), 5);
}

TEST(TensorflowParserTest, TestConversionInt32) {
  Tensor tensor;
  tensor.data_type = DataType::kInt32;
  tensor.tensor_content = {"5"};
  tensor.tensor_shape = {1, 1};

  const absl::StatusOr<tensorflow::Tensor> result =
      ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const tensorflow::Tensor tf_tensor = *std::move(result);

  tensorflow::DataType data_type = tf_tensor.dtype();
  EXPECT_EQ(data_type, tensorflow::DataType::DT_INT32);

  EXPECT_EQ(tf_tensor.flat<int>()(0), 5);
}

TEST(TensorflowParserTest, TestConversion_BatchSizeOne) {
  Tensor tensor;
  tensor.data_type = DataType::kInt64;
  tensor.tensor_content = {"7"};
  tensor.tensor_shape = {1, 1};

  // Call the conversion function
  const absl::StatusOr<tensorflow::Tensor> result =
      ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const tensorflow::Tensor tf_tensor = *std::move(result);

  // Check the dimensions of the resulting tensor
  tensorflow::TensorShape tensorShape = tf_tensor.shape();
  EXPECT_EQ(tensorShape.dims(), 2);
  EXPECT_EQ(tensorShape.dim_size(0), 1);
  EXPECT_EQ(tensorShape.dim_size(1), 1);

  // Check the values in the tensor
  EXPECT_EQ(tf_tensor.flat<int64_t>()(0), 7);
}

TEST(TensorflowParserTest, TestConversion_WrongType) {
  Tensor tensor;
  tensor.data_type = DataType::kInt64;
  tensor.tensor_content = {"seven"};  // Incompatible type.
  tensor.tensor_shape = {1, 1};

  const absl::StatusOr result = ConvertFlatArrayToTensor(tensor);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(result.status().message(), "Error in int64 conversion");
}

TEST(TensorflowParserTest, ConvertTensorsOrErrorToJson) {
  tensorflow::TensorShape shape({2, 3});  // 2x3 tensor
  tensorflow::Tensor tensor(tensorflow::DT_FLOAT, shape);

  // Access and modify tensor elements.
  auto tensor_data = tensor.flat<float>();
  for (int i = 0; i < 6; ++i) {
    tensor_data(i) = i * 2.0f;  // Set values (i * 2)
  }
  std::vector<TensorWithName> tensors;
  tensors.push_back(TensorWithName("output", tensor));
  std::vector<TensorsOrError> batch_tensors = {
      {.model_path = "my_bucket/models/pcvr_models/1/", .tensors = tensors}};

  auto output = ConvertTensorsOrErrorToJson(batch_tensors);
  EXPECT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(
      output.value(),
      R"({"response":[{"model_path":"my_bucket/models/pcvr_models/1/","tensors":[{"tensor_name":"output","tensor_shape":[2,3],"data_type":"FLOAT","tensor_content":[0.0,2.0,4.0,6.0,8.0,10.0]}]}]})");
}

TEST(TensorflowParserTest, ConvertTensorsOrErrorToJson_UnsupportedFloat16Type) {
  tensorflow::Tensor half_tensor(tensorflow::DT_BFLOAT16);

  std::vector<TensorWithName> tensors;
  tensors.push_back(TensorWithName("output", half_tensor));
  std::vector<TensorsOrError> batch_tensors = {
      {.model_path = "my_bucket/models/pcvr_models/1/", .tensors = tensors}};

  auto output = ConvertTensorsOrErrorToJson(batch_tensors);
  EXPECT_FALSE(output.ok());
  EXPECT_EQ(output.status().message(), "Unsupported data type 14");
}

TEST(TensorflowParserTest, ConvertTensorsOrErrorToJson_int8) {
  std::vector<int8_t> int8_data = {10, -5};
  tensorflow::Tensor int8_tensor(tensorflow::DT_INT8, {2});
  auto int8_data_ptr = int8_tensor.flat<int8_t>().data();
  std::copy(int8_data.begin(), int8_data.end(), int8_data_ptr);

  std::vector<TensorWithName> tensors;
  tensors.push_back(TensorWithName("output", int8_tensor));
  std::vector<TensorsOrError> batch_tensors = {
      {.model_path = "my_bucket/models/pcvr_models/1/", .tensors = tensors}};

  auto output = ConvertTensorsOrErrorToJson(batch_tensors);
  EXPECT_TRUE(output.ok());
  EXPECT_EQ(
      output.value(),
      R"({"response":[{"model_path":"my_bucket/models/pcvr_models/1/","tensors":[{"tensor_name":"output","tensor_shape":[2],"data_type":"INT8","tensor_content":[10,-5]}]}]})");
}

TEST(TensorflowParserTest, ConvertTensorsOrErrorToJson_int16) {
  std::vector<int16_t> int16_data = {10, -5};
  tensorflow::Tensor int16_tensor(tensorflow::DT_INT16, {2});
  auto int16_data_ptr = int16_tensor.flat<int16_t>().data();
  std::copy(int16_data.begin(), int16_data.end(), int16_data_ptr);

  std::vector<TensorWithName> tensors;
  tensors.push_back(TensorWithName("output", int16_tensor));
  std::vector<TensorsOrError> batch_tensors;
  batch_tensors.push_back(
      {.model_path = "my_bucket/models/pcvr_models/1/", .tensors = tensors});

  auto output = ConvertTensorsOrErrorToJson(batch_tensors);
  EXPECT_TRUE(output.ok());
  EXPECT_EQ(
      output.value(),
      R"({"response":[{"model_path":"my_bucket/models/pcvr_models/1/","tensors":[{"tensor_name":"output","tensor_shape":[2],"data_type":"INT16","tensor_content":[10,-5]}]}]})");
}

TEST(TensorflowParserTest, TestConvertTensorsOrErrorToJson_MultipleTensors) {
  // Double tensor.
  tensorflow::TensorShape shape_1({1, 1});
  tensorflow::Tensor tensor_1(tensorflow::DT_DOUBLE, shape_1);
  auto tensor_data_1 = tensor_1.flat<double>();
  tensor_data_1(0) = 3.14;

  // Int64 tensor.
  tensorflow::TensorShape shape_2({1, 1});
  tensorflow::Tensor tensor_2(tensorflow::DT_INT64, shape_2);
  auto tensor_data_2 = tensor_2.flat<int64_t>();
  tensor_data_2(0) = 1000;

  std::vector<TensorWithName> tensors;
  tensors.push_back(TensorWithName("output1", tensor_1));
  tensors.push_back(TensorWithName("output2", tensor_2));

  std::vector<TensorsOrError> batch_tensors = {
      {.model_path = "my_bucket/models/pcvr_models/1/", .tensors = tensors}};

  auto output = ConvertTensorsOrErrorToJson(batch_tensors);
  EXPECT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(
      output.value(),
      R"({"response":[{"model_path":"my_bucket/models/pcvr_models/1/","tensors":[{"tensor_name":"output1","tensor_shape":[1,1],"data_type":"DOUBLE","tensor_content":[3.14]},{"tensor_name":"output2","tensor_shape":[1,1],"data_type":"INT64","tensor_content":[1000]}]}]})");
}

TEST(TensorflowParserTest, TestConvertTensorsOrErrorToJson_BatchOfModels) {
  // Double tensor.
  tensorflow::TensorShape shape_1({1, 1});
  tensorflow::Tensor tensor_1(tensorflow::DT_DOUBLE, shape_1);
  auto tensor_data_1 = tensor_1.flat<double>();
  tensor_data_1(0) = 3.14;
  std::vector<TensorWithName> tensors1;
  tensors1.push_back(TensorWithName("output1", tensor_1));

  // Int64 tensor.
  tensorflow::TensorShape shape_2({1, 1});
  tensorflow::Tensor tensor_2(tensorflow::DT_INT64, shape_2);
  auto tensor_data_2 = tensor_2.flat<int64_t>();
  tensor_data_2(0) = 1000;
  std::vector<TensorWithName> tensors2;
  tensors2.push_back(TensorWithName("output2", tensor_2));

  std::vector<TensorsOrError> batch_tensors = {
      {.model_path = "my_bucket/models/pcvr_models/1/", .tensors = tensors1},
      {.model_path = "my_bucket/models/pctr_models/1/", .tensors = tensors2}};

  auto output = ConvertTensorsOrErrorToJson(batch_tensors);
  EXPECT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(
      output.value(),
      R"({"response":[{"model_path":"my_bucket/models/pcvr_models/1/","tensors":[{"tensor_name":"output1","tensor_shape":[1,1],"data_type":"DOUBLE","tensor_content":[3.14]}]},{"model_path":"my_bucket/models/pctr_models/1/","tensors":[{"tensor_name":"output2","tensor_shape":[1,1],"data_type":"INT64","tensor_content":[1000]}]}]})");
}

TEST(TensorflowParserTest, TestConvertTensorsOrErrorToJson_PartialResult) {
  // Double tensor.
  tensorflow::TensorShape shape_1({1, 1});
  tensorflow::Tensor tensor_1(tensorflow::DT_DOUBLE, shape_1);
  auto tensor_data_1 = tensor_1.flat<double>();
  tensor_data_1(0) = 3.14;
  TensorsOrError tensors = {.model_path = "my_bucket/models/pcvr_models/1/",
                            .tensors = std::vector<TensorWithName>{
                                TensorWithName("output1", tensor_1)}};
  TensorsOrError error = {.model_path = "my_bucket/models/pctr_models/1/",
                          .error = Error{.error_type = Error::MODEL_NOT_FOUND,
                                         .description = "Model not found."}};

  std::vector<TensorsOrError> batch_tensors = {tensors, error};

  auto output = ConvertTensorsOrErrorToJson(batch_tensors);
  EXPECT_TRUE(output.ok()) << output.status();
  EXPECT_EQ(
      output.value(),
      R"({"response":[{"model_path":"my_bucket/models/pcvr_models/1/","tensors":[{"tensor_name":"output1","tensor_shape":[1,1],"data_type":"DOUBLE","tensor_content":[3.14]}]},{"model_path":"my_bucket/models/pctr_models/1/","error":{"error_type":"MODEL_NOT_FOUND","description":"Model not found."}}]})");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
