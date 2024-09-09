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
goog.module('common.privateAggregation.test');
const testSuite = goog.require('goog.testing.testSuite');
goog.require('goog.testing.asserts');
goog.require('goog.testing.jsunit');

testSuite({
  /** @return {void} */
  testIsUnsigned128BitInteger() {},

  /** @return {void} */
  testConvertTo128BitArray() {},

  /** @return {void} */
  testConvertToBucketOffset() {},

  /** @return {void} */
  testIsValidBucket() {},

  /** @return {void} */
  testIsValidValue_ValidNumber() {
    assertTrue(privateAggregationUtil.isValidValue(10));
    assertTrue(privateAggregationUtil.isValidValue(0));
  },

  /**
   * @return {void}
   * @suppress {reportUnknownTypes}
   */
  testIsValidValue_InvalidNumber() {
    const error_msg = 'Value must be an unsigned 32-bit integer.';
    const error_name = 'RangeError';
    try {
      privateAggregationUtil.isValidValue(-1);
    } catch (e) {
      assertEquals(error_msg, e.message);
      assertEquals(error_name, e.name);
    }

    try {
      privateAggregationUtil.isValidValue(-(2 ** 128));
    } catch (e) {
      assertEquals(error_msg, e.message);
      assertEquals(error_name, e.name);
    }
  },

  /** @return {void} */
  testIsValidValue_ValidSignalValue() {
    const validSignalValue = {
      baseValue: BaseValue.WINNING_BID,
      scale: 10,
      offset: 5,
    };
    assertTrue(privateAggregationUtil.isValidValue(validSignalValue));
  },

  /** @return {void} */
  testIsValidValue_InvalidValue_MissingBaseValue() {
    const invalidValue = {
      scale: 10,
      offset: 5,
    };
    assertFalse(privateAggregationUtil.isValidValue(invalidValue));
  },

  /** @return {void} */
  testIsValidValue_invalidSignalValue_InvalidBaseValue() {
    const invalidSignalValue = {
      baseValue: 'invalid',
      scale: 10,
      offset: 5,
    };
    assertFalse(privateAggregationUtil.isValidValue(invalidSignalValue));
  },

  /** @return {void} */
  testIsValidValue_InvalidSignalValue_InvalidScale() {
    const invalidSignalValue = {
      baseValue: BaseValue.WINNING_BID,
      scale: 'invalid',
      offset: 5,
    };
    assertFalse(privateAggregationUtil.isValidValue(invalidSignalValue));
  },

  /** @return {void} */
  testIsValidValue_InvalidSignalValue_InvalidOffset() {
    const invalidSignalValue = {
      baseValue: BaseValue.WINNING_BID,
      scale: 10,
      offset: 'invalid',
    };
    assertFalse(privateAggregationUtil.isValidValue(invalidSignalValue));
  },

  /** @return {void} */
  testIsValidCustomEvent_ValidEventType() {
    const validEventOne = 'my_event';
    const validEventTwo = 'another_event';
    assertTrue(privateAggregationUtil.isValidCustomEvent(validEventOne));
    assertTrue(privateAggregationUtil.isValidCustomEvent(validEventTwo));
  },

  /** @return {void} */
  testIsValidCustomEvent_InvalidEventType_Reserved() {
    const invalidEventOne = 'reserved.my_event';
    const invalidEventTwo = 'reserved.another_event';
    assertFalse(privateAggregationUtil.isValidCustomEvent(invalidEventOne));
    assertFalse(privateAggregationUtil.isValidCustomEvent(invalidEventTwo));
  },

  /**
   * @return {void}
   * @suppress {checkTypes}
   */
  testIsValidCustomEvent_InvalidEventType_NotString() {
    assertFalse(privateAggregationUtil.isValidCustomEvent(123));
    assertFalse(privateAggregationUtil.isValidCustomEvent(null));
    assertFalse(privateAggregationUtil.isValidCustomEvent(undefined));
  },

  /**
   * @return {void}
   * @suppress {reportUnknownTypes}
   */
  testCreateContribution() {
    const numberBucket = 12345;
    const numberValue = 5;
    const expectedContribution = {
      bucket: { bucket_128_bit: { bucket_128_bits: [12345, 0] } },
      value: { int_value: numberValue },
    };
    const contribution = privateAggregationUtil.createContribution(numberBucket, numberValue);
    assertObjectEquals(expectedContribution, contribution);
  },
  /** @return {void} */
  /** @suppress {reportUnknownTypes, checkTypes} */
  testConvertBaseValueForWinningBid() {
    var result = privateAggregationUtil.convertBaseValueToEnumString('winning-bid');
    assertEquals(result, 'BASE_VALUE_WINNING_BID');
  },
  /** @return {void} */
  /** @suppress {reportUnknownTypes, checkTypes} */
  testConvertBaseValueForHighestScoringOtherBid() {
    var result = privateAggregationUtil.convertBaseValueToEnumString('highest-scoring-other-bid');
    assertEquals(result, 'BASE_VALUE_HIGHEST_SCORING_OTHER_BID');
  },
  /** @return {void} */
  /** @suppress {reportUnknownTypes, checkTypes} */
  testConvertBaseValueForScriptRunTime() {
    var result = privateAggregationUtil.convertBaseValueToEnumString('script-run-time');
    assertEquals(result, 'BASE_VALUE_SCRIPT_RUN_TIME');
  },
  /** @return {void} */
  /** @suppress {reportUnknownTypes, checkTypes} */
  testConvertBaseValueForInvalidString() {
    try {
      privateAggregationUtil.convertBaseValueToEnumString('something-else');
      fail('Expected TypeError for unsupported base value');
    } catch (error) {
      assertTrue(error instanceof TypeError);
    }
  },
  /** @return {void} */
  /** @suppress {reportUnknownTypes, checkTypes} */
  testConvertBaseValueWithNullInput() {
    try {
      privateAggregationUtil.convertBaseValueToEnumString(null);
      fail('Expected TypeError for null base value');
    } catch (error) {
      assertTrue(error instanceof TypeError);
    }
  },

  /**
   * @return {void}
   * @suppress {reportUnknownTypes}
   */
  testCreateContributionWhenBucketAndValueAreObjects() {
    const objectBucket = {
      baseValue: 'winning-bid',
      scale: 2.0,
      offset: 1,
    };
    const bucketOffset = {
      value: [1, 0],
      is_negative: false,
    };
    const signalBucket = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 2.0,
      offset: bucketOffset,
    };
    const objectValue = {
      baseValue: 'winning-bid',
      scale: 10,
      offset: 5,
    };
    const signalValue = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 10,
      offset: 5,
    };
    const expectedContribution2 = {
      bucket: { signal_bucket: signalBucket },
      value: { extended_value: signalValue },
    };

    const contribution2 = privateAggregationUtil.createContribution(objectBucket, objectValue);
    assertObjectEquals(expectedContribution2, contribution2);
  },

  /**
   * @return {void}
   * @suppress {reportUnknownTypes}
   */
  testCreateContributionWithDefaultSignalBucketScaleAndOffset() {
    const objectBucket = {
      baseValue: 'winning-bid',
    };
    const objectValue = {
      baseValue: 'winning-bid',
    };
    const signalValue = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 1.0,
      offset: 0,
    };
    const bucketOffset = {
      value: [0, 0],
      is_negative: false,
    };
    const signalBucket = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 1.0,
      offset: bucketOffset,
    };
    const expectedContribution2 = {
      bucket: { signal_bucket: signalBucket },
      value: { extended_value: signalValue },
    };

    const contribution2 = privateAggregationUtil.createContribution(objectBucket, objectValue);
    assertObjectEquals(expectedContribution2, contribution2);
  },
  /**
   * @return {void}
   * @suppress {reportUnknownTypes}
   */
  testCreateContributionWithNegativeSignalBucketOffset() {
    const objectBucket = {
      baseValue: 'winning-bid',
      scale: 2.0,
      offset: -1,
    };
    const objectValue = {
      baseValue: 'winning-bid',
      scale: 10,
      offset: -5,
    };
    const signalValue = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 10,
      offset: -5,
    };
    const bucketOffset = {
      value: [1, 0],
      is_negative: true,
    };
    const signalBucket = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 2.0,
      offset: bucketOffset,
    };
    const expectedContribution2 = {
      bucket: { signal_bucket: signalBucket },
      value: { extended_value: signalValue },
    };

    const contribution2 = privateAggregationUtil.createContribution(objectBucket, objectValue);
    assertObjectEquals(expectedContribution2, contribution2);
  },

  /**
   * @return {void}
   * @suppress {reportUnknownTypes}
   */
  testCreateContributionWhenBucketAndValueAreInvalid() {
    const invalidBucket = 'invalid';
    const invalidValue = 'invalid';

    try {
      privateAggregationUtil.createContribution(invalidBucket, invalidValue);
    } catch (e) {
      assertEquals(e.message, 'Invalid type for Private Aggregation bucket.');
      assertEquals(e.name, 'TypeError');
    }
  },
  /**
   * @return {void}
   * @suppress {reportUnknownTypes}
   */
  testCreateContributionWithInvalidValue() {
    const numberBucket = 12345;
    const invalidValue = 'invalid';

    try {
      privateAggregationUtil.createContribution(numberBucket, invalidValue);
    } catch (e) {
      assertEquals(e.message, 'Invalid type for Private Aggregation value.');
      assertEquals(e.name, 'TypeError');
    }
  },
});
