# Privacy Sandbox - ML Inference in Bidding and Auction Services

This directory contains the implementation of the inference sidecar, designed to execute ML
inference with the bidding server.

For a comprehensive overview, please refer to the
[Inference Overview](https://github.com/privacysandbox/protected-auction-services-docs/blob/main/inference_overview.md).

## Status

Experimental. A special build flag is required for Docker image creation.

-   Platform: Currrently supports GCP. AWS support is in progress.
-   ML Frameworks: TensorFlow and PyTorch are supported.

## Build the B&A Inference Docker Image

Use the `production/packaging/build_and_test_all_in_docker` script to build and push Docker images
of B&A to your GCP image repo. Please make sure you use `inference_non_prod` build flag.

Here's how to enable TensorFlow v2.14:

```shell
cd services/inference_sidecar/modules/tensorflow_v2_14_0
./builders/tools/bazel-debian run //:generate_artifacts

export BAZEL_EXTRA_ARGS=--//:inference_runtime=tensorflow
cd $(git rev-parse --show-toplevel)
./production/packaging/build_and_test_all_in_docker \
  --service-path bidding_service --instance gcp --platform gcp \
  --no-precommit --no-tests \
  --gcp-image-repo $REPO_PATH \
  --build-flavor inference_non_prod --gcp-image-tag $IMAGE_TAG
unset BAZEL_EXTRA_ARGS
```

Similarly, to enable PyTorch v2.1, you can navigate to the
`services/inference_sidecar/modules/pytorch_v2_1_1` directory and execute the `generate_artifacts`
tool. During the `build_and_test_all_in_docker` step, replace the BAZEL_EXTRA_ARGS environment
variable with `--//:inference_runtime=pytorch`.

## Start the B&A servers in GCP

-   Create a GCS bucket and store ML models into it.
-   The GCS bucket needs to give the service account associated with the GCE instance both storage
    legacy bucket reader and storage legacy object reader permissions:
    [guide on setting bucket permissions](https://cloud.google.com/storage/docs/access-control/using-iam-permissions#bucket-add).
-   Now you need to deploy a B&A server stack:
    [deployment guide](https://github.com/privacysandbox/bidding-auction-servers/tree/main/production/deploy/gcp/terraform/environment/demo/README.md).
-   In the B&A deployment terraform configuration, set runtime flags:

    -   Set `INFERENCE_SIDECAR_BINARY_PATH` to `/server/bin/inference_sidecar`.
    -   Set `INFERENCE_MODEL_BUCKET_NAME` to the name of the GCS bucket that you have created. Note
        that this _not_ a url. For example, the bucket name can be "test_models".
    -   Set `INFERENCE_MODEL_BUCKET_PATHS` to a comma separated list of model paths under the
        bucket. For example, within the "test_models" bucket there are saved models
        "tensorflow/model1" and "tensorflow/model1". To load these two models, this flag should be
        set to "tensorflow/model1,tensorflow/model2".
    -   Set `INFERENCE_SIDECAR_RUNTIME_CONFIG` which has the following format:

        ```javascript
        {
            "num_interop_threads": <integer_value>,
            "num_intraop_threads": <integer_value>,
            "module_name": <string_value>,
            "cpuset": <an array of integer values>
        }
        ```

        `module_name` flag is required and should be one of "test", "tensorflow_v2_14_0", or
        "pytorch_v2_1_1". All other flags are optional.

## Start the B&A servers locally for testing/debugging

The servers run locally and the ML models are directly read from the local disk.

-   Use the command-line flags: `--inference_sidecar_binary_path` and
    `--inference_model_local_paths`, `--inference_sidecar_runtime_config`.
-   Utilize services/inference_sidecar/common/tools/debug/start_inference script.

## Trigger ML Inference

To invoke ML inference within generateBid() or any other UDF:

1.  Create a batchInferenceRequest object. For each model:

    -   Specify the `model_path` (where the model is located).
    -   Specify input `tensors` containing the data for the model.

2.  Serialize the batchInferenceRequest into a JSON string compatible with the inference API.

3.  Trigger inference.

    -   Call the runInference(json_string) function, passing your serialized request as the
        argument. This initiates the inference execution.

4.  Process results.
    -   Parse the inference output returned by `runInference()`.
    -   Extract the model predictions for your bid calculations.

Here's the UDF example code:

```javascript
function generateBid() {
    const batchInferenceRequest = {
        request: [
            {
                model_path: '/model/path',
                tensors: [
                    {
                        tensor_name: 'input',
                        data_type: 'INT32',
                        tensor_shape: [1, 1],
                        tensor_content: ['8'],
                    },
                ],
            },
        ],
    };

    const jsonRequest = JSON.stringify(batchInferenceRequest);
    const inferenceResult = runInference(jsonRequest);

    // Implement parsing logic based on your model's output format.
    const bidValue = parseInferenceResult(inferenceResult);
    return { bid: bidValue };
}
```

## Inference API

A Batch Inference Request (called `request`in JSON API) is an array of single inference requests.
Each single inference request contains an obligatory `model_path` string field (the same path you
used to register the ML model), as well as `tensors` array field. Each input tensor has three
required fields: `data_type`, `tensor_shape` and `tensor_content`. Tensorflow additionally requires
`tensor_name` field which should match tensor's name in SavedModel.

`tensor_shape` is an array representing the shape of tensor. The order of entries indicates the
layout of the values in the tensor in-memory representation. The first entry is the outermost
dimension, which usually represents the input batch size. The last entry is the innermost dimension.
We support only dense tensors.

`tensor_content` holds the flattened representation of the tensor which can be reconstructed using
`tensor_shape`. Only the representation corresponding to `data_type` field can be set. The number of
elements in `tensor_content` should be equal to the product of tensor_shape elements, for example a
tensor of shape `[1, 4]` will expect a flat array or 4 elements (e.g. `["1", "2", "7", "4"]`), and
one with a shape `[2,3]` will expect a 6 element one.

Currently we support 6 tensor types (both in Tensorflow and PyTorch): double, float, int8, int16,
int32, int64.

All numbers in `tensor_content` MUST be surrounded by quotes.

See an example of a Batch Inference Request in a JSON format with 2 models:

```json
{
    "request": [
        {
            "model_path": "my_bucket/models/pcvr/1/",
            "tensors": [
                {
                    "tensor_name": "feature1",
                    "data_type": "DOUBLE",
                    "tensor_shape": [2, 1],
                    "tensor_content": ["0.454920", "-0.25752"]
                }
            ]
        },
        {
            "model_path": "my_bucket/models/pctr/2/",
            "tensors": [
                {
                    "tensor_name": "feature1",
                    "data_type": "INT32",
                    "tensor_shape": [2, 1],
                    "tensor_content": ["5", "6"]
                },
                {
                    "tensor_name": "feature2",
                    "data_type": "FLOAT",
                    "tensor_shape": [2, 3],
                    "tensor_content": ["0.5", "0.6", "0.7", "0.8", "0.21", "-0.99"]
                }
            ]
        }
    ]
}
```

-   Please refer to
    [request_parser.h](https://github.com/privacysandbox/bidding-auction-servers/tree/main/services/inference_sidecar/common/utils/request_parser.h)
    and
    [inference_payload.proto](https://github.com/privacysandbox/bidding-auction-servers/tree/main/services/inference_sidecar/common/proto/inference_payload.proto).
-   Note: Protocol buffer API support is not currently available; `inference_payload.proto` provides
    a documentation-only schema.
