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

#include "utils/request_proto_parser.h"

#include <utility>

#include "absl/status/statusor.h"
#include "googletest/include/gtest/gtest.h"
#include "proto/inference_payload.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

TEST(Test, ConvertJsonToProto_Failure_EmptyString) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto("", parsing_errors);
  ASSERT_FALSE(output.ok()) << output.status();
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(output.status().message(),
            "JSON Parse Error: The document is empty.");
}

TEST(Test, ConvertJsonToProto_Failure_WrongFormat) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto("1.0", parsing_errors);
  ASSERT_FALSE(output.ok()) << output.status();
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(output.status().message(), "Missing request in the JSON document");
}

constexpr char kJsonStringBasic[] = R"json({
  "request" : [{
    "model_path" : "my_bucket/models/pcvr_models/1/",
    "tensors" : [
    {
      "data_type": "INT64",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["1"]
    }
  ]
}]
    })json";

TEST(Test, ConvertJsonToProto_Success_BasicInput) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kJsonStringBasic, parsing_errors);
  ASSERT_TRUE(output.ok()) << output.status();
  ASSERT_EQ(output.value().request_size(), 1);
  const InferenceRequestProto parsed_data = output.value().request(0);
  EXPECT_EQ(parsed_data.model_path(), "my_bucket/models/pcvr_models/1/");
  EXPECT_EQ(parsed_data.tensors_size(), 1);
  constexpr char kExpectedString[] =
      "model_path: \"my_bucket/models/pcvr_models/1/\"\n"
      "tensors {\n"
      "  data_type: INT64\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_content {\n"
      "    tensor_content_int64: 1\n"
      "  }\n"
      "}\n";
  ASSERT_EQ(parsed_data.DebugString(), kExpectedString);
  ASSERT_EQ(parsing_errors.response_size(), 0);
}

constexpr char kJsonString[] = R"json({
  "request" : [{
    "model_path" : "my_bucket/models/pcvr/1/",
    "tensors" : [
    {
      "tensor_name": "scalar1",
      "data_type": "INT64",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["1"]
    },
    {
      "tensor_name": "scalar2",
      "data_type": "INT64",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["2"]
    },
    {
      "tensor_name": "scalar3",
      "data_type": "INT64",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["3"]
    },
    {
      "tensor_name": "scalar4",
      "data_type": "INT64",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["4"]
    },
    {
      "tensor_name": "scalar5",
      "data_type": "INT64",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["5"]
    },
    {
      "tensor_name": "embedding1",
      "data_type": "FLOAT",
      "tensor_shape": [
        1,
        3
      ],
      "tensor_content": ["0.32", "0.12", "0.98"]
    },
    {
      "tensor_name": "embedding2",
      "data_type": "FLOAT",
      "tensor_shape": [
        1,
        3
      ],
      "tensor_content": ["0.62", "0.18", "0.23"]
    }
  ]
}]
    })json";

TEST(Test, ConvertJsonToProto_Success_SmallModel) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kJsonString, parsing_errors);
  ASSERT_TRUE(output.ok()) << output.status();
  ASSERT_EQ(output.value().request_size(), 1);
  const InferenceRequestProto parsed_data = output.value().request(0);
  ASSERT_EQ(parsed_data.model_path(), "my_bucket/models/pcvr/1/");
  ASSERT_EQ(parsed_data.tensors_size(), 7);
  constexpr char kExpectedString[] =
      "model_path: \"my_bucket/models/pcvr/1/\"\n"
      "tensors {\n"
      "  data_type: INT64\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar1\"\n"
      "  tensor_content {\n"
      "    tensor_content_int64: 1\n"
      "  }\n"
      "}\n"
      "tensors {\n"
      "  data_type: INT64\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar2\"\n"
      "  tensor_content {\n"
      "    tensor_content_int64: 2\n"
      "  }\n"
      "}\n"
      "tensors {\n"
      "  data_type: INT64\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar3\"\n"
      "  tensor_content {\n"
      "    tensor_content_int64: 3\n"
      "  }\n"
      "}\n"
      "tensors {\n"
      "  data_type: INT64\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar4\"\n"
      "  tensor_content {\n"
      "    tensor_content_int64: 4\n"
      "  }\n"
      "}\n"
      "tensors {\n"
      "  data_type: INT64\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar5\"\n"
      "  tensor_content {\n"
      "    tensor_content_int64: 5\n"
      "  }\n"
      "}\n"
      "tensors {\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 3\n"
      "  tensor_name: \"embedding1\"\n"
      "  tensor_content {\n"
      "    tensor_content_float: 0.32\n"
      "    tensor_content_float: 0.12\n"
      "    tensor_content_float: 0.98\n"
      "  }\n"
      "}\n"
      "tensors {\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 3\n"
      "  tensor_name: \"embedding2\"\n"
      "  tensor_content {\n"
      "    tensor_content_float: 0.62\n"
      "    tensor_content_float: 0.18\n"
      "    tensor_content_float: 0.23\n"
      "  }\n"
      "}\n";
  ASSERT_EQ(parsed_data.DebugString(), kExpectedString);
  ASSERT_EQ(parsing_errors.response_size(), 0);
}

constexpr char kJsonStringTwoModels[] = R"json({
  "request" : [
    {
      "model_path" : "my_bucket/models/pcvr/1/",
      "tensors" : [
        {
          "tensor_name": "scalar1",
          "data_type": "INT64",
          "tensor_shape": [1, 1],
          "tensor_content": ["1"]
        }
      ]
    },
    {
      "model_path" : "my_bucket/models/pctr/2/",
      "tensors" : [
        {
          "tensor_name": "vector1",
          "data_type": "FLOAT",
          "tensor_shape": [1, 4],
          "tensor_content": ["0.5", "0.6", "0.7", "0.8"]
        }
      ]
    }
  ]
})json";

TEST(Test, ConvertJsonToProto_Success_TwoModels) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kJsonStringTwoModels, parsing_errors);
  ASSERT_TRUE(output.ok()) << output.status();
  ASSERT_EQ(output.value().request_size(), 2);
  constexpr char kExpectedStringPcvrModel[] =
      "model_path: \"my_bucket/models/pcvr/1/\"\n"
      "tensors {\n"
      "  data_type: INT64\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar1\"\n"
      "  tensor_content {\n"
      "    tensor_content_int64: 1\n"
      "  }\n"
      "}\n";
  constexpr char kExpectedStringPctrModel[] =
      "model_path: \"my_bucket/models/pctr/2/\"\n"
      "tensors {\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 4\n"
      "  tensor_name: \"vector1\"\n"
      "  tensor_content {\n"
      "    tensor_content_float: 0.5\n"
      "    tensor_content_float: 0.6\n"
      "    tensor_content_float: 0.7\n"
      "    tensor_content_float: 0.8\n"
      "  }\n"
      "}\n";
  bool found_pcvr = false, found_pctr = false;
  for (int i = 0; i < output.value().request_size(); ++i) {
    const InferenceRequestProto& request = output.value().request(i);
    std::string model_path = request.model_path();
    if (model_path == "my_bucket/models/pcvr/1/") {
      found_pcvr = true;
      ASSERT_EQ(request.DebugString(), kExpectedStringPcvrModel);
    } else if (model_path == "my_bucket/models/pctr/2/") {
      found_pctr = true;
      ASSERT_EQ(request.DebugString(), kExpectedStringPctrModel);
    }
  }
  ASSERT_TRUE(found_pcvr);
  ASSERT_TRUE(found_pctr);
}

constexpr char kJsonStringIntTypes[] = R"json({
  "request" : [{
    "model_path" : "my_bucket/models/pcvr/1/",
    "tensors" : [
    {
      "tensor_name": "scalar1",
      "data_type": "INT8",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["2"]
    },
    {
      "tensor_name": "scalar2",
      "data_type": "INT16",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["2"]
    },
    {
      "tensor_name": "scalar3",
      "data_type": "INT32",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["2"]
    }
  ]
}]
    })json";

// Proto uses INT32 to represent INT8, INT16, and INT32.
TEST(Test, ConvertJsonToProto_Success_IntTypes) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kJsonStringIntTypes, parsing_errors);
  ASSERT_TRUE(output.ok()) << output.status();
  ASSERT_EQ(output.value().request_size(), 1);
  const InferenceRequestProto parsed_data = output.value().request(0);
  ASSERT_EQ(parsed_data.model_path(), "my_bucket/models/pcvr/1/");
  ASSERT_EQ(parsed_data.tensors_size(), 3);
  constexpr char kExpectedString[] =
      "model_path: \"my_bucket/models/pcvr/1/\"\n"
      "tensors {\n"
      "  data_type: INT8\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar1\"\n"
      "  tensor_content {\n"
      "    tensor_content_int32: 2\n"
      "  }\n"
      "}\n"
      "tensors {\n"
      "  data_type: INT16\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar2\"\n"
      "  tensor_content {\n"
      "    tensor_content_int32: 2\n"
      "  }\n"
      "}\n"
      "tensors {\n"
      "  data_type: INT32\n"
      "  tensor_shape: 1\n"
      "  tensor_shape: 1\n"
      "  tensor_name: \"scalar3\"\n"
      "  tensor_content {\n"
      "    tensor_content_int32: 2\n"
      "  }\n"
      "}\n";
  EXPECT_EQ(parsed_data.DebugString(), kExpectedString);
}

constexpr char kJsonStringFloat16[] = R"json({
  "request" : [{
    "model_path" : "my_bucket/models/pcvr/1/",
    "tensors" : [
    {
      "tensor_name": "scalar1",
      "data_type": "FLOAT16",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["1.2"]
    }
  ]
}]
    })json";

TEST(Test, ConvertJsonToProto_Failure_UnsupportedFloat16Type) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kJsonStringFloat16, parsing_errors);
  ASSERT_FALSE(output.ok()) << output.status();
  ASSERT_EQ(parsing_errors.response_size(), 1);
  OrderedInferenceErrorResponse error = parsing_errors.response(0);
  EXPECT_EQ(error.index(), 0);
  EXPECT_EQ(error.response().error().description(),
            "Invalid JSON format: Unsupported 'data_type' field FLOAT16");
}

constexpr char kJsonStringInt8[] = R"json({
  "request" : [{
    "model_path" : "my_bucket/models/pcvr/1/",
    "tensors" : [
    {
      "data_type": "INT8",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["130"]
    }
  ]
}]
    })json";

TEST(Test, ConvertJsonToProto_Failure_Int8OutOfBound) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kJsonStringInt8, parsing_errors);
  ASSERT_FALSE(output.ok()) << output.status();
  ASSERT_EQ(parsing_errors.response_size(), 1);
  OrderedInferenceErrorResponse error = parsing_errors.response(0);
  EXPECT_EQ(error.index(), 0);
  EXPECT_EQ(error.response().error().description(),
            "The number is outside of bounds of int8_t.");
}

constexpr char kJsonStringInt16[] = R"json({
  "request" : [{
    "model_path" : "my_bucket/models/pcvr/1/",
    "tensors" : [
    {
      "data_type": "INT16",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["32768"]
    }
  ]
}]
    })json";

TEST(Test, ConvertJsonToProto_Failure_Int16OutOfBound) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kJsonStringInt16, parsing_errors);
  ASSERT_FALSE(output.ok()) << output.status();
  ASSERT_EQ(parsing_errors.response_size(), 1);
  OrderedInferenceErrorResponse error = parsing_errors.response(0);
  EXPECT_EQ(error.index(), 0);
  EXPECT_EQ(error.response().error().description(),
            "The number is outside of bounds of int16_t.");
}

constexpr char kTwoInvalidInputs[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": [3.14]
    }
  ]
},
{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "UNSUPPORTED",
      "tensor_shape": [
        1
      ],
    "tensor_content": ["1"]
    }
  ]
}]
    })json";

TEST(Test, ConvertJsonToProto_Failure_TwoInvalidInputs) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kTwoInvalidInputs, parsing_errors);
  ASSERT_FALSE(output.ok()) << output.status();
  ASSERT_EQ(parsing_errors.response_size(), 2);
  EXPECT_EQ(output.status().message(), "All requests are invalid");
  OrderedInferenceErrorResponse error0 = parsing_errors.response(0);
  EXPECT_EQ(error0.index(), 0);
  EXPECT_EQ(error0.response().error().description(),
            "All numbers within the tensor_content must be enclosed in quotes");
  OrderedInferenceErrorResponse error1 = parsing_errors.response(1);
  EXPECT_EQ(error1.index(), 1);
  EXPECT_EQ(error1.response().error().description(),
            "Invalid JSON format: Unsupported 'data_type' field UNSUPPORTED");
}

constexpr char kValidandInvalidInputs[] = R"json({
  "request" : [{
    "model_path" : "simple_model_1",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
},
{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "UNSUPPORTED",
      "tensor_shape": [
        1
      ],
    "tensor_content": ["1"]
    }
  ]
},
{
    "model_path" : "simple_model_2",
    "tensors" : [
    {
      "data_type": "INT8",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["1"]
    }
  ]
},
{
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
}
]
    })json";

TEST(Test, ConvertJsonToProto_Success_ValidandInvalidInputs) {
  BatchOrderedInferenceErrorResponse parsing_errors;
  const absl::StatusOr<BatchInferenceRequest> output =
      ConvertJsonToProto(kValidandInvalidInputs, parsing_errors);
  ASSERT_TRUE(output.ok()) << output.status();
  const InferenceRequestProto parsed_data_0 = output.value().request(0);
  constexpr char kExpectedString0[] =
      "model_path: \"simple_model_1\"\ntensors {\n  data_type: DOUBLE\n  "
      "tensor_shape: 1\n  tensor_content {\n    tensor_content_double: 3.14\n  "
      "}\n}\n";
  ASSERT_EQ(parsed_data_0.DebugString(), kExpectedString0);
  const InferenceRequestProto parsed_data_1 = output.value().request(1);
  constexpr char kExpectedString1[] =
      "model_path: \"simple_model_2\"\ntensors {\n  data_type: INT8\n  "
      "tensor_shape: 1\n  tensor_content {\n    tensor_content_int32: 1\n  "
      "}\n}\n";
  ASSERT_EQ(parsed_data_1.DebugString(), kExpectedString1);
  ASSERT_EQ(parsing_errors.response_size(), 2);
  OrderedInferenceErrorResponse error0 = parsing_errors.response(0);
  EXPECT_EQ(error0.index(), 1);
  EXPECT_EQ(error0.response().error().description(),
            "Invalid JSON format: Unsupported 'data_type' field UNSUPPORTED");
  OrderedInferenceErrorResponse error1 = parsing_errors.response(1);
  EXPECT_EQ(error1.index(), 3);
  EXPECT_EQ(error1.response().error().description(),
            "Missing model_path in the JSON document");
}

TEST(Test, MergeBatchResponse_Success) {
  BatchInferenceResponse responses;
  BatchOrderedInferenceErrorResponse indexed_responses;
  for (int i = 0; i < 5; i++) {
    if (i % 2 == 0) {
      OrderedInferenceErrorResponse indexed_response;
      indexed_response.set_index(i);
      InferenceResponseProto* response = indexed_response.mutable_response();
      response->set_model_path("model_" + std::to_string(i));
      *indexed_responses.add_response() = std::move(indexed_response);
    } else {
      InferenceResponseProto response;
      response.set_model_path("model_" + std::to_string(i));
      *responses.add_response() = std::move(response);
    }
  }
  absl::StatusOr<BatchInferenceResponse> result =
      MergeBatchResponse(responses, indexed_responses);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(
      result.value().DebugString(),
      "response {\n  model_path: \"model_0\"\n}\nresponse {\n  model_path: "
      "\"model_1\"\n}\nresponse {\n  model_path: \"model_2\"\n}\nresponse {\n  "
      "model_path: \"model_3\"\n}\nresponse {\n  model_path: \"model_4\"\n}\n");
}

TEST(Test, MergeBatchResponse_Success_OnlyError) {
  BatchInferenceResponse responses;
  BatchOrderedInferenceErrorResponse indexed_responses;
  for (int i = 0; i < 5; i++) {
    OrderedInferenceErrorResponse indexed_response;
    indexed_response.set_index(i);
    InferenceResponseProto* response = indexed_response.mutable_response();
    response->set_model_path("model_" + std::to_string(i));
    *indexed_responses.add_response() = std::move(indexed_response);
  }
  absl::StatusOr<BatchInferenceResponse> result =
      MergeBatchResponse(responses, indexed_responses);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(
      result.value().DebugString(),
      "response {\n  model_path: \"model_0\"\n}\nresponse {\n  model_path: "
      "\"model_1\"\n}\nresponse {\n  model_path: \"model_2\"\n}\nresponse {\n  "
      "model_path: \"model_3\"\n}\nresponse {\n  model_path: \"model_4\"\n}\n");
}

TEST(Test, MergeBatchResponse_Success_NoError) {
  BatchInferenceResponse responses;
  BatchOrderedInferenceErrorResponse indexed_responses;
  for (int i = 0; i < 5; i++) {
    InferenceResponseProto response;
    response.set_model_path("model_" + std::to_string(i));
    *responses.add_response() = std::move(response);
  }
  absl::StatusOr<BatchInferenceResponse> result =
      MergeBatchResponse(responses, indexed_responses);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(
      result.value().DebugString(),
      "response {\n  model_path: \"model_0\"\n}\nresponse {\n  model_path: "
      "\"model_1\"\n}\nresponse {\n  model_path: \"model_2\"\n}\nresponse {\n  "
      "model_path: \"model_3\"\n}\nresponse {\n  model_path: \"model_4\"\n}\n");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
