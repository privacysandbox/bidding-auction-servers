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

#include "utils/request_parser.h"

#include <string>

#include "absl/status/statusor.h"
#include "googletest/include/gtest/gtest.h"
#include "gtest/gtest.h"
#include "proto/inference_sidecar.pb.h"
#include "utils/inference_error_code.h"
#include "utils/test_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

TEST(Test, Failure_EmptyString) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest("");
  ASSERT_FALSE(output.ok());
  EXPECT_EQ(output.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(output.status().message(),
            "JSON Parse Error: The document is empty.");
}

TEST(Test, Failure_WrongFormat) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest("1.0");
  ASSERT_FALSE(output.ok());
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

TEST(Test, Success_BasicInput) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kJsonStringBasic);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 1);
  ASSERT_TRUE(output.value()[0].request);
  const InferenceRequest parsed_data = output.value()[0].request.value();
  ASSERT_EQ(parsed_data.model_path, "my_bucket/models/pcvr_models/1/");
  std::vector<Tensor> inputs = parsed_data.inputs;
  ASSERT_EQ(inputs.size(), 1);
  constexpr char kExpectedString[] =
      R"(Model path: my_bucket/models/pcvr_models/1/
Tensors: [
Data type: INT64
Tensor shape: [1, 1]
Tensor content: [1]
]
)";
  ASSERT_EQ(parsed_data.DebugString(), kExpectedString);
}

constexpr char kJsonStringWithNumber[] = R"json({
  "request" : [{
    "model_path" : "my_bucket/models/pcvr_models/1/",
    "tensors" : [
    {
      "data_type": "INT64",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": [1]
    }
  ]
}]
    })json";

TEST(Test, Failure_NumberInsteadOfString) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kJsonStringWithNumber);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 1);
  ASSERT_TRUE(output.value()[0].error);
  const Error error = output.value()[0].error.value();
  EXPECT_EQ(error.description,
            "All numbers within the tensor_content must be enclosed in quotes");
  EXPECT_EQ(error.model_path, "my_bucket/models/pcvr_models/1/");
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

TEST(Test, SmallModel) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kJsonString);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 1);
  ASSERT_TRUE(output.value()[0].request);
  const InferenceRequest parsed_data = output.value()[0].request.value();
  ASSERT_EQ(parsed_data.model_path, "my_bucket/models/pcvr/1/");
  std::vector<Tensor> inputs = parsed_data.inputs;
  ASSERT_EQ(inputs.size(), 7);
  constexpr char kExpectedString[] =
      R"(Model path: my_bucket/models/pcvr/1/
Tensors: [
Tensor name: scalar1
Data type: INT64
Tensor shape: [1, 1]
Tensor content: [1]

Tensor name: scalar2
Data type: INT64
Tensor shape: [1, 1]
Tensor content: [2]

Tensor name: scalar3
Data type: INT64
Tensor shape: [1, 1]
Tensor content: [3]

Tensor name: scalar4
Data type: INT64
Tensor shape: [1, 1]
Tensor content: [4]

Tensor name: scalar5
Data type: INT64
Tensor shape: [1, 1]
Tensor content: [5]

Tensor name: embedding1
Data type: FLOAT
Tensor shape: [1, 3]
Tensor content: [0.32, 0.12, 0.98]

Tensor name: embedding2
Data type: FLOAT
Tensor shape: [1, 3]
Tensor content: [0.62, 0.18, 0.23]
]
)";
  ASSERT_EQ(parsed_data.DebugString(), kExpectedString);
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

TEST(Test, TwoModels) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kJsonStringTwoModels);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 2);
  ASSERT_TRUE(output.value()[0].request);

  constexpr char kExpectedStringPcvrModel[] =
      R"(Model path: my_bucket/models/pcvr/1/
Tensors: [
Tensor name: scalar1
Data type: INT64
Tensor shape: [1, 1]
Tensor content: [1]
]
)";

  constexpr char kExpectedStringPctrModel[] =
      R"(Model path: my_bucket/models/pctr/2/
Tensors: [
Tensor name: vector1
Data type: FLOAT
Tensor shape: [1, 4]
Tensor content: [0.5, 0.6, 0.7, 0.8]
]
)";

  ASSERT_TRUE(output.value()[0].request);
  const InferenceRequest pcvr_data = output.value()[0].request.value();
  ASSERT_EQ(pcvr_data.model_path, "my_bucket/models/pcvr/1/");
  ASSERT_EQ(pcvr_data.DebugString(), kExpectedStringPcvrModel);
  ASSERT_TRUE(output.value()[1].request);
  const InferenceRequest pctr_data = output.value()[1].request.value();
  ASSERT_EQ(pctr_data.model_path, "my_bucket/models/pctr/2/");
  ASSERT_EQ(pctr_data.DebugString(), kExpectedStringPctrModel);
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

TEST(Test, IntTypes) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kJsonStringIntTypes);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 1);
  ASSERT_TRUE(output.value()[0].request);
  const InferenceRequest parsed_data = output.value()[0].request.value();

  EXPECT_EQ(parsed_data.model_path, "my_bucket/models/pcvr/1/");
  std::vector<Tensor> inputs = parsed_data.inputs;
  EXPECT_EQ(inputs.size(), 3);
  constexpr char kExpectedString[] =
      R"(Model path: my_bucket/models/pcvr/1/
Tensors: [
Tensor name: scalar1
Data type: INT8
Tensor shape: [1, 1]
Tensor content: [2]

Tensor name: scalar2
Data type: INT16
Tensor shape: [1, 1]
Tensor content: [2]

Tensor name: scalar3
Data type: INT32
Tensor shape: [1, 1]
Tensor content: [2]
]
)";
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

TEST(Test, UnsupportedFloat16Type) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kJsonStringFloat16);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 1);
  ASSERT_TRUE(output.value()[0].error);
  const Error error = output.value()[0].error.value();
  EXPECT_EQ(error.description,
            "Invalid JSON format: Unsupported 'data_type' field FLOAT16");
  EXPECT_EQ(error.model_path, "my_bucket/models/pcvr/1/");
}

constexpr char kJsonStringNoModelPath[] = R"json({
  "request" : [{
    "tensors" : [
    {
      "tensor_name": "scalar1",
      "data_type": "INT8",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["1"]
    }
  ]
}]
    })json";

TEST(Test, MissingModelPath) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kJsonStringNoModelPath);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 1);
  ASSERT_TRUE(output.value()[0].error);
  const Error error = output.value()[0].error.value();
  EXPECT_EQ(error.description, "Missing model_path in the JSON document");
  EXPECT_EQ(error.model_path, "");
}

constexpr char kBothValidAndInvalidInputs[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
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
      "data_type": "INT16",
      "tensor_shape": [
        0
      ],
    "tensor_content": ["1"]
    }
  ]
}]
    })json";

TEST(Test, BothValidAndInvalidInputs) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kBothValidAndInvalidInputs);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 2);
  ASSERT_TRUE(output.value()[0].request);
  const InferenceRequest parsed_data = output.value()[0].request.value();
  constexpr char kExpectedString[] =
      R"(Model path: simple_model
Tensors: [
Data type: DOUBLE
Tensor shape: [1]
Tensor content: [3.14]
]
)";
  ASSERT_EQ(parsed_data.DebugString(), kExpectedString);

  ASSERT_TRUE(output.value()[1].error);
  const Error error = output.value()[1].error.value();
  EXPECT_EQ(error.description,
            "Invalid tensor dimension: it has to be greater than 0");
  EXPECT_EQ(error.model_path, "simple_model");
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

TEST(Test, TwoInvalidInputs) {
  const absl::StatusOr<std::vector<ParsedRequestOrError>> output =
      ParseJsonInferenceRequest(kTwoInvalidInputs);
  ASSERT_TRUE(output.ok());
  ASSERT_EQ(output.value().size(), 2);
  ASSERT_TRUE(output.value()[0].error);
  const Error error_0 = output.value()[0].error.value();
  EXPECT_EQ(error_0.description,
            "All numbers within the tensor_content must be enclosed in quotes");
  ASSERT_TRUE(output.value()[1].error);
  const Error error_1 = output.value()[1].error.value();
  EXPECT_EQ(error_1.description,
            "Invalid JSON format: Unsupported 'data_type' field UNSUPPORTED");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
