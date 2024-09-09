// Copyright 2024 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//      http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

goog.module('bidding_service.privateAggregation.test');
const jsunit = goog.require('goog.testing.jsunit');
const testSuite = goog.require('goog.testing.testSuite');
goog.require('goog.testing.asserts');

testSuite({
  /** @return {void} */
  testContributeToHistogramOnEvent_ValidNumericBucketAndValue() {
    const numberBucket = 5;
    const numberValue = 10;

    const contribution = {
      bucket: numberBucket,
      value: numberValue,
    };

    const expectedContribution1 = {
      bucket: { bucket_128_bit: { bucket_128_bits: [numberBucket, 0] } },
      value: { int_value: numberValue },
      event: {
        event_type: 'EVENT_TYPE_WIN',
      },
    };
    const expectedContribution2 = {
      bucket: { bucket_128_bit: { bucket_128_bits: [numberBucket, 0] } },
      value: { int_value: numberValue },
      event: {
        event_type: 'EVENT_TYPE_CUSTOM',
        event_name: 'a-custom-event-test',
      },
    };
    // Test with win.
    privateAggregation.contributeToHistogramOnEvent('reserved.win', contribution);
    assertObjectEquals(expectedContribution1, private_aggregation_contributions[0]);
    // Test with custom.
    privateAggregation.contributeToHistogramOnEvent('a-custom-event-test', contribution);
    assertObjectEquals(expectedContribution2, private_aggregation_contributions[1]);
  },
  testContributeToHistogramOnEvent_ValidObjectBucketAndValue() {
    const objectBucket = {
      baseValue: 'winning-bid',
      scale: 1.0,
      offset: 10,
    };
    const signalBucket = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 1.0,
      offset: {
        value: [10, 0],
        is_negative: false,
      },
    };
    const objectValue = {
      baseValue: 'winning-bid',
      scale: 30,
      offset: 5,
    };
    const signalValue = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 30,
      offset: 5,
    };
    const contribution = {
      bucket: objectBucket,
      value: objectValue,
    };

    const expectedContribution1 = {
      bucket: { signal_bucket: signalBucket },
      value: { extended_value: signalValue },
      event: {
        event_type: 'EVENT_TYPE_WIN',
      },
    };
    const expectedContribution2 = {
      bucket: { signal_bucket: signalBucket },
      value: { extended_value: signalValue },
      event: {
        event_type: 'EVENT_TYPE_CUSTOM',
        event_name: 'a-custom-event-test',
      },
    };
    // Test with win.
    privateAggregation.contributeToHistogramOnEvent('reserved.win', contribution);
    privateAggregation.contributeToHistogramOnEvent('a-custom-event-test', contribution);
    assertObjectEquals(expectedContribution1, private_aggregation_contributions[0]);
    // Test with custom.
    assertObjectEquals(expectedContribution2, private_aggregation_contributions[1]);
  },

  testContributeToHistogram() {
    const numberBucket = 5;
    const numberValue = 10;

    const contribution = {
      bucket: numberBucket,
      value: numberValue,
    };

    const expectedContribution1 = {
      bucket: { bucket_128_bit: { bucket_128_bits: [numberBucket, 0] } },
      value: { int_value: numberValue },
      event: {
        event_type: 'EVENT_TYPE_ALWAYS', // reserved.always
      },
    };
    privateAggregation.contributeToHistogram(contribution);
    assertObjectEquals(expectedContribution1, private_aggregation_contributions[0]);
  },
  /** @return {void} */
  tearDown() {
    private_aggregation_contributions.length = 0;
  },
});
