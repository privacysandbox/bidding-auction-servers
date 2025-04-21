
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
#include <string>
#include <vector>

#include "absl/strings/str_format.h"
#include "proto/inference_payload.pb.h"
#include "src/util/status_macro/status_macros.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "utils/error.h"
#include "utils/request_proto_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

absl::StatusOr<tensorflow::Tensor> ConvertProtoToTensor(
    const TensorProto& tensor_proto) {
  tensorflow::TensorShape shape;
  for (int dim : tensor_proto.tensor_shape()) {
    shape.AddDim(dim);
  }
  const TensorContent& tensor_content = tensor_proto.tensor_content();
  if (tensor_proto.data_type() == DataTypeProto::FLOAT) {
    tensorflow::Tensor tf_tensor(tensorflow::DataTypeToEnum<float>::v(), shape);
    auto tensor_mapped = tf_tensor.flat<float>();
    for (int i = 0; i < tensor_content.tensor_content_float_size(); ++i) {
      tensor_mapped(i) = tensor_content.tensor_content_float(i);
    }
    return tf_tensor;
  } else if (tensor_proto.data_type() == DataTypeProto::DOUBLE) {
    tensorflow::Tensor tf_tensor(tensorflow::DataTypeToEnum<double>::v(),
                                 shape);
    auto tensor_mapped = tf_tensor.flat<double>();
    for (int i = 0; i < tensor_content.tensor_content_double_size(); ++i) {
      tensor_mapped(i) = tensor_content.tensor_content_double(i);
    }
    return tf_tensor;
  } else if (tensor_proto.data_type() == DataTypeProto::INT8) {
    tensorflow::Tensor tf_tensor(tensorflow::DataTypeToEnum<int8_t>::v(),
                                 shape);
    auto tensor_mapped = tf_tensor.flat<int8_t>();
    for (int i = 0; i < tensor_content.tensor_content_int32_size(); ++i) {
      tensor_mapped(i) = tensor_content.tensor_content_int32(i);
    }
    return tf_tensor;
  } else if (tensor_proto.data_type() == DataTypeProto::INT16) {
    tensorflow::Tensor tf_tensor(tensorflow::DataTypeToEnum<int16_t>::v(),
                                 shape);
    auto tensor_mapped = tf_tensor.flat<int16_t>();
    for (int i = 0; i < tensor_content.tensor_content_int32_size(); ++i) {
      tensor_mapped(i) = tensor_content.tensor_content_int32(i);
    }
    return tf_tensor;
  } else if (tensor_proto.data_type() == DataTypeProto::INT32) {
    tensorflow::Tensor tf_tensor(tensorflow::DataTypeToEnum<int32_t>::v(),
                                 shape);
    auto tensor_mapped = tf_tensor.flat<int32_t>();
    for (int i = 0; i < tensor_content.tensor_content_int32_size(); ++i) {
      tensor_mapped(i) = tensor_content.tensor_content_int32(i);
    }
    return tf_tensor;
  } else if (tensor_proto.data_type() == DataTypeProto::INT64) {
    tensorflow::Tensor tf_tensor(tensorflow::DataTypeToEnum<int64_t>::v(),
                                 shape);
    auto tensor_mapped = tf_tensor.flat<int64_t>();
    for (int i = 0; i < tensor_content.tensor_content_int64_size(); ++i) {
      tensor_mapped(i) = tensor_content.tensor_content_int64(i);
    }
    return tf_tensor;
  } else {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unsupported data type %d", tensor_proto.data_type()));
  }
}

absl::StatusOr<TensorProto> ConvertTensorToProto(
    const std::string tensor_name, const tensorflow::Tensor& tensor) {
  TensorProto tensor_proto;
  tensor_proto.set_tensor_name(tensor_name);
  switch (tensor.dtype()) {
    case tensorflow::DT_FLOAT:
      tensor_proto.set_data_type(DataTypeProto::FLOAT);
      break;
    case tensorflow::DT_DOUBLE:
      tensor_proto.set_data_type(DataTypeProto::DOUBLE);
      break;
    case tensorflow::DT_INT8:
      tensor_proto.set_data_type(DataTypeProto::INT8);
      break;
    case tensorflow::DT_INT16:
      tensor_proto.set_data_type(DataTypeProto::INT16);
      break;
    case tensorflow::DT_INT32:
      tensor_proto.set_data_type(DataTypeProto::INT32);
      break;
    case tensorflow::DT_INT64:
      tensor_proto.set_data_type(DataTypeProto::INT64);
      break;
    default:
      return absl::InvalidArgumentError("Unsupported tensor type: " +
                                        std::to_string(tensor.dtype()));
  }
  size_t num_elements = 1;
  for (int64_t dim : tensor.shape().dim_sizes()) {
    tensor_proto.add_tensor_shape(dim);
    num_elements *= dim;
  }
  switch (tensor_proto.data_type()) {
    case DataTypeProto::FLOAT: {
      const float* data = tensor.flat<float>().data();
      tensor_proto.mutable_tensor_content()
          ->mutable_tensor_content_float()
          ->Resize(num_elements, 0.0f);
      std::copy(data, data + num_elements,
                tensor_proto.mutable_tensor_content()
                    ->mutable_tensor_content_float()
                    ->begin());
      break;
    }
    case DataTypeProto::DOUBLE: {
      const double* data = tensor.flat<double>().data();
      tensor_proto.mutable_tensor_content()
          ->mutable_tensor_content_double()
          ->Resize(num_elements, 0.0);
      std::copy(data, data + num_elements,
                tensor_proto.mutable_tensor_content()
                    ->mutable_tensor_content_double()
                    ->begin());
      break;
    }
    case DataTypeProto::INT8: {
      const int8_t* data = tensor.flat<int8_t>().data();
      tensor_proto.mutable_tensor_content()
          ->mutable_tensor_content_int32()
          ->Resize(num_elements, 0);
      std::copy(data, data + num_elements,
                tensor_proto.mutable_tensor_content()
                    ->mutable_tensor_content_int32()
                    ->begin());
      break;
    }
    case DataTypeProto::INT16: {
      const int16_t* data = tensor.flat<int16_t>().data();
      tensor_proto.mutable_tensor_content()
          ->mutable_tensor_content_int32()
          ->Resize(num_elements, 0);
      std::copy(data, data + num_elements,
                tensor_proto.mutable_tensor_content()
                    ->mutable_tensor_content_int32()
                    ->begin());
      break;
    }
    case DataTypeProto::INT32: {
      const int32_t* data = tensor.flat<int32_t>().data();
      tensor_proto.mutable_tensor_content()
          ->mutable_tensor_content_int32()
          ->Resize(num_elements, 0);
      std::copy(data, data + num_elements,
                tensor_proto.mutable_tensor_content()
                    ->mutable_tensor_content_int32()
                    ->begin());
      break;
    }
    case DataTypeProto::INT64: {
      const int64_t* data = tensor.flat<int64_t>().data();
      tensor_proto.mutable_tensor_content()
          ->mutable_tensor_content_int64()
          ->Resize(num_elements, 0);
      std::copy(data, data + num_elements,
                tensor_proto.mutable_tensor_content()
                    ->mutable_tensor_content_int64()
                    ->begin());
      break;
    }
  }
  return tensor_proto;
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
