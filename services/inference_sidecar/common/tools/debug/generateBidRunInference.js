/**
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
function generateBid(interest_group, auction_signals, buyer_signals, trusted_bidding_signals, device_signals) {
  const inference_result = run();
  const bid = Math.floor(inference_result * 10);
  console.log('Inference result: ', inference_result, ', bid: ', bid);
  if (interest_group.adRenderIds === undefined || interest_group.adRenderIds.length == 0) {
    return {};
  }
  if (interest_group.name === 'winningCA') {
    return {
      render: 'https://performance-fledge-static-5jyy5ulagq-uc.a.run.app/' + interest_group.adRenderIds[0],
      ad: { tbsLength: 12345 },
      bid: 1742,
      allowComponentAuction: false,
    };
  } /*Reshaped into an AdWithBid.*/
  return {
    render: 'https://performance-fledge-static-5jyy5ulagq-uc.a.run.app/' + interest_group.adRenderIds[0],
    ad: { tbsLength: 12345 },
    bid: bid,
    allowComponentAuction: false,
  };
}

const inferenceRequest = {
  model_path: '/generate_bid_model',
  tensors: [
    {
      tensor_name: 'serving_default_embedding_input:0',
      data_type: 'DOUBLE',
      tensor_shape: [1, 10],
      tensor_content: ['0.11', '0.22', '0.33', '0.11', '0.22', '0.33', '0.11', '0.22', '0.33', '0.11'],
    },
  ],
};

const batchInferenceRequest = {
  request: [inferenceRequest],
};

function run() {
  console.log('Run started');
  // Convert the JS object to a JSON string. JSON uses a schema defined in
  // services/inference_sidecar/common/proto/inference_payload.proto.
  const json_string = JSON.stringify(batchInferenceRequest);
  console.log(json_string);
  const output = runInference(json_string);
  console.log('RunInference output: ', output);
  const tensorContent = JSON.parse(output).response[0].tensors[0].tensor_content;
  return tensorContent.reduce((partialSum, a) => partialSum + a, 0);
}
