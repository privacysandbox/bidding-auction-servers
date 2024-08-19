//  Copyright 2023 Google LLC
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

#include "pytorch_parser.h"

#include <utility>

#include <torch/script.h>
#include <torch/torch.h>

#include "absl/status/statusor.h"
#include "googletest/include/gtest/gtest.h"
#include "utils/request_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

TEST(PyTorchModuleTest, TestConversion) {
  Tensor tensor;
  tensor.data_type = DataType::kFloat;
  tensor.tensor_content = {"1.2", "-2", "3", "4", "5", "6"};
  tensor.tensor_shape = {2, 3};

  // Call the conversion function
  const absl::StatusOr<torch::Tensor> result = ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const torch::Tensor torch_tensor = *std::move(result);

  // Check the dimensions of the resulting tensor
  EXPECT_EQ(torch_tensor.dim(), 2);
  EXPECT_EQ(torch_tensor.size(0), 2);
  EXPECT_EQ(torch_tensor.size(1), 3);

  // Check the values in the tensor
  EXPECT_FLOAT_EQ(torch_tensor[0][0].item<float>(), 1.2);
  EXPECT_FLOAT_EQ(torch_tensor[0][1].item<float>(), -2.0);
  EXPECT_FLOAT_EQ(torch_tensor[0][2].item<float>(), 3.0);
  EXPECT_FLOAT_EQ(torch_tensor[1][0].item<float>(), 4.0);
  EXPECT_FLOAT_EQ(torch_tensor[1][1].item<float>(), 5.0);
  EXPECT_FLOAT_EQ(torch_tensor[1][2].item<float>(), 6.0);
}

TEST(PyTorchModuleTest, TestConversionInt8) {
  Tensor tensor;
  tensor.data_type = DataType::kInt8;
  tensor.tensor_content = {"5"};
  tensor.tensor_shape = {1};

  const absl::StatusOr<torch::Tensor> result = ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const torch::Tensor torch_tensor = *std::move(result);

  caffe2::TypeMeta data_type = torch_tensor.dtype();
  EXPECT_EQ(data_type, torch::ScalarType::Char);

  EXPECT_EQ(torch_tensor[0].item<int8_t>(), 5);
}

TEST(PyTorchModuleTest, TestConversionInt16) {
  Tensor tensor;
  tensor.data_type = DataType::kInt16;
  tensor.tensor_content = {"5"};
  tensor.tensor_shape = {1};

  const absl::StatusOr<torch::Tensor> result = ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const torch::Tensor torch_tensor = *std::move(result);

  caffe2::TypeMeta data_type = torch_tensor.dtype();
  EXPECT_EQ(data_type, torch::ScalarType::Short);

  EXPECT_EQ(torch_tensor[0].item<short>(), 5);
}

TEST(PyTorchModuleTest, TestConversionInt32) {
  Tensor tensor;
  tensor.data_type = DataType::kInt32;
  tensor.tensor_content = {"5"};
  tensor.tensor_shape = {1};

  const absl::StatusOr<torch::Tensor> result = ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const torch::Tensor torch_tensor = *std::move(result);

  caffe2::TypeMeta data_type = torch_tensor.dtype();
  EXPECT_EQ(data_type, torch::ScalarType::Int);

  EXPECT_EQ(torch_tensor[0].item<int>(), 5);
}

TEST(PyTorchModuleTest, TestConversionInt64) {
  Tensor tensor;
  tensor.data_type = DataType::kInt64;
  tensor.tensor_content = {"5"};
  tensor.tensor_shape = {1};

  const absl::StatusOr<torch::Tensor> result = ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const torch::Tensor torch_tensor = *std::move(result);

  caffe2::TypeMeta data_type = torch_tensor.dtype();
  EXPECT_EQ(data_type, torch::ScalarType::Long);

  EXPECT_EQ(torch_tensor[0].item<long>(), 5);
}

TEST(PyTorchModuleTest, TestConversion_BatchSizeOne) {
  Tensor tensor;
  tensor.data_type = DataType::kInt64;
  tensor.tensor_content = {"7"};
  tensor.tensor_shape = {1, 1};

  // Call the conversion function
  const absl::StatusOr<torch::Tensor> result = ConvertFlatArrayToTensor(tensor);
  EXPECT_TRUE(result.ok());
  const torch::Tensor torch_tensor = *std::move(result);

  // Check the dimensions of the resulting tensor
  EXPECT_EQ(torch_tensor.dim(), 2);
  EXPECT_EQ(torch_tensor.size(0), 1);
  EXPECT_EQ(torch_tensor.size(1), 1);

  // Check the values in the tensor
  EXPECT_EQ(torch_tensor[0][0].item<int64_t>(), 7);
}

TEST(PyTorchModuleTest, TestConversion_WrongType) {
  Tensor tensor;
  tensor.data_type = DataType::kInt64;
  tensor.tensor_content = {"seven"};  // Incompatible type.
  tensor.tensor_shape = {1, 1};

  const absl::StatusOr result = ConvertFlatArrayToTensor(tensor);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(result.status().message(), "Error in int64 conversion");
}

TEST(PyTorchModuleTest, ConvertBatchOutputsToJson_UnsupportedType) {
  PerModelOutput output;
  output.model_path = "/path/to/model";
  torch::IValue tensor_ivalue = torch::tensor({1, 2, 3}, torch::kByte);
  output.inference_output = tensor_ivalue;

  std::vector<PerModelOutput> batch_outputs = {output};

  absl::StatusOr<std::string> result = ConvertBatchOutputsToJson(batch_outputs);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), "Unsupported type unsigned char");
}

TEST(PyTorchModuleTest, ConvertBatchOutputsToJson_SimpleTest) {
  PerModelOutput output;
  output.model_path = "/path/to/model";
  torch::IValue tensor_ivalue = torch::tensor({1, 2, 3});
  output.inference_output = tensor_ivalue;

  std::vector<PerModelOutput> batch_outputs = {output};

  absl::StatusOr<std::string> result = ConvertBatchOutputsToJson(batch_outputs);
  ASSERT_TRUE(result.ok());

  const std::string expected_json =
      R"({"response":[{"model_path":"/path/to/model","tensors":[{"tensor_shape":[3],"data_type":"INT64","tensor_content":[1,2,3]}]}]})";

  EXPECT_EQ(result.value(), expected_json);
}

TEST(PyTorchModuleTest, ConvertBatchOutputsToJsonTest_FloatDoubleInputTypes) {
  PerModelOutput output;
  output.model_path = "/path/to/model";
  torch::IValue double_tensor =
      torch::tensor({{1.5, 2., 3.}, {4., 5., 6.}}, torch::kDouble);
  torch::IValue float_tensor = torch::tensor({3.14}, torch::kFloat);
  // Use a tuple
  std::vector<torch::IValue> elements = {double_tensor, float_tensor};
  torch::IValue tuple = c10::ivalue::Tuple::create(elements);
  output.inference_output = tuple;

  std::vector<PerModelOutput> batch_outputs = {output};

  absl::StatusOr<std::string> result = ConvertBatchOutputsToJson(batch_outputs);
  EXPECT_EQ(result.status().message(), "");
  EXPECT_TRUE(result.ok());

  const std::string expected_json =
      R"({"response":[{"model_path":"/path/to/model","tensors":[{"tensor_shape":[2,3],"data_type":"DOUBLE","tensor_content":[1.5,2.0,3.0,4.0,5.0,6.0]},{"tensor_shape":[1],"data_type":"FLOAT","tensor_content":[3.140000104904175]}]}]})";

  EXPECT_EQ(result.value(), expected_json);
}

TEST(PyTorchModuleTest, ConvertBatchOutputsToJsonTest_IntTypes) {
  PerModelOutput output;
  output.model_path = "/path/to/model";
  torch::IValue int8_tensor = torch::tensor({1}, torch::ScalarType::Char);
  torch::IValue int16_tensor = torch::tensor({1}, torch::ScalarType::Short);
  torch::IValue int32_tensor = torch::tensor({1}, torch::ScalarType::Int);
  torch::IValue int64_tensor = torch::tensor({1}, torch::ScalarType::Long);
  // Use a tuple
  std::vector<torch::IValue> elements = {int8_tensor, int16_tensor,
                                         int32_tensor, int64_tensor};
  torch::IValue tuple = c10::ivalue::Tuple::create(elements);
  output.inference_output = tuple;

  std::vector<PerModelOutput> batch_outputs = {output};

  absl::StatusOr<std::string> result = ConvertBatchOutputsToJson(batch_outputs);
  EXPECT_EQ(result.status().message(), "");
  EXPECT_TRUE(result.ok());

  const std::string expected_json =
      R"({"response":[{"model_path":"/path/to/model","tensors":[{"tensor_shape":[1],"data_type":"INT8","tensor_content":[1]},{"tensor_shape":[1],"data_type":"INT16","tensor_content":[1]},{"tensor_shape":[1],"data_type":"INT32","tensor_content":[1]},{"tensor_shape":[1],"data_type":"INT64","tensor_content":[1]}]}]})";

  EXPECT_EQ(result.value(), expected_json);
}

TEST(PyTorchModuleTest, ConvertBatchOutputsToJsonTest_2Models) {
  PerModelOutput output1;
  output1.model_path = "/path/to/model/1";
  torch::IValue tensor_ivalue1 = torch::tensor({1, 2, 3});
  output1.inference_output = tensor_ivalue1;

  PerModelOutput output2;
  output2.model_path = "/path/to/model/2";
  torch::IValue tensor_ivalue2 = torch::tensor({125}, torch::kDouble);
  output2.inference_output = tensor_ivalue2;

  std::vector<PerModelOutput> batch_outputs = {output1, output2};

  absl::StatusOr<std::string> result = ConvertBatchOutputsToJson(batch_outputs);
  ASSERT_TRUE(result.ok());

  const std::string expected_json =
      R"({"response":[{"model_path":"/path/to/model/1","tensors":[{"tensor_shape":[3],"data_type":"INT64","tensor_content":[1,2,3]}]},{"model_path":"/path/to/model/2","tensors":[{"tensor_shape":[1],"data_type":"DOUBLE","tensor_content":[125.0]}]}]})";

  EXPECT_EQ(result.value(), expected_json);
}

TEST(PyTorchModuleTest, TestConvertBatchOutput_PartialResult) {
  PerModelOutput output1;
  output1.model_path = "/path/to/model/1";
  torch::IValue tensor_ivalue1 = torch::tensor({1, 2, 3});
  output1.inference_output = tensor_ivalue1;

  PerModelOutput output2;
  output2.model_path = "/path/to/model/2";
  output2.error = Error{.error_type = Error::MODEL_NOT_FOUND,
                        .description = "Model not found."};

  std::vector<PerModelOutput> batch_outputs = {output1, output2};

  absl::StatusOr<std::string> result = ConvertBatchOutputsToJson(batch_outputs);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(
      result.value(),
      R"({"response":[{"model_path":"/path/to/model/1","tensors":[{"tensor_shape":[3],"data_type":"INT64","tensor_content":[1,2,3]}]},{"model_path":"/path/to/model/2","error":{"error_type":"MODEL_NOT_FOUND","description":"Model not found."}}]})");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
