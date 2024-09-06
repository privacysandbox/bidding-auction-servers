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
      base_value: BaseValue.WINNING_BID,
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
      base_value: 'invalid',
      scale: 10,
      offset: 5,
    };
    assertFalse(privateAggregationUtil.isValidValue(invalidSignalValue));
  },

  /** @return {void} */
  testIsValidValue_InvalidSignalValue_InvalidScale() {
    const invalidSignalValue = {
      base_value: BaseValue.WINNING_BID,
      scale: 'invalid',
      offset: 5,
    };
    assertFalse(privateAggregationUtil.isValidValue(invalidSignalValue));
  },

  /** @return {void} */
  testIsValidValue_InvalidSignalValue_InvalidOffset() {
    const invalidSignalValue = {
      base_value: BaseValue.WINNING_BID,
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

  /** @return {void} */
  testIsValidContribution() {},

  /**
   * TODO(b/355034881): All usage and test cases of "number" type for bucket should be replaced as "bigint" once bigint support is enabled.
   * Validation of SignalBucket and SignalValue's fields should also be added.
   */
  /**
   * @return {void}
   * @suppress {reportUnknownTypes}
   */
  testCreateContribution() {
    const numberBucket = 5;
    const numberValue = 5;

    const expectedContribution = {
      bucket_128_bit: numberBucket,
      signal_bucket: null,
      int_value: numberValue,
      extended_value: null,
    };

    const contribution = privateAggregationUtil.createContribution(numberBucket, numberValue);
    assertObjectEquals(expectedContribution, contribution);

    const objectBucket = {
      value: [5, 0],
      is_negative: false,
    };
    const objectValue = {
      base_value: 'winning-bid',
      int_value: 10,
      is_negative: false,
    };

    const expectedContribution2 = {
      bucket_128_bit: null,
      signal_bucket: objectBucket,
      int_value: null,
      extended_value: objectValue,
    };

    const contribution2 = privateAggregationUtil.createContribution(objectBucket, objectValue);
    assertObjectEquals(expectedContribution2, contribution2);

    const invalidBucket = 'invalid';
    const invalidValue = 'invalid';

    try {
      privateAggregationUtil.createContribution(invalidBucket, invalidValue);
    } catch (e) {
      assertEquals(e.message, 'Invalid type for Private Aggregation bucket.');
      assertEquals(e.name, 'TypeError');
    }

    try {
      privateAggregationUtil.createContribution(numberBucket, invalidValue);
    } catch (e) {
      assertEquals(e.message, 'Invalid type for Private Aggregation value.');
      assertEquals(e.name, 'TypeError');
    }
  },
});
