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

goog.module('auction_service.privateAggregation.test');
const jsunit = goog.require('goog.testing.jsunit');
const testSuite = goog.require('goog.testing.testSuite');
goog.require('goog.testing.asserts');

testSuite({
  /** @return {void} */
  /** @return {void} */
  testContributeToHistogramOnEvent_ValidNumericBucketAndValue() {
    const contribution = {
      bucket: 10,
      value: 30,
    };

    // Test with ReservedEventConstants
    const reservedWin = 'reserved.win';
    assertEquals(ReservedEventConstants.WIN, reservedWin);
    privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, contribution);
    assertEquals(privateAggregationContributions.win.length, 1);
    // Assert the expected fields and values directly
    assertEquals(privateAggregationContributions.win[0].bucket.bucket_128_bit.bucket_128_bits[0], 10);
    assertEquals(privateAggregationContributions.win[0].bucket.bucket_128_bit.bucket_128_bits[1], 0);
    assertEquals(privateAggregationContributions.win[0].value.int_value, 30);
  },

  /** @return {void} */
  /** @return {void} */
  testContributeToHistogramOnEventWithCustomEvent_ValidNumericBucketAndValue() {
    const contribution = {
      bucket: 10,
      value: 30,
    };

    const customEventName = 'my_custom_event';

    // Test with custom event
    privateAggregation.contributeToHistogramOnEvent(customEventName, contribution);
    assertEquals(privateAggregationContributions.custom_events[customEventName].length, 1);
    // Assert the expected fields and values directly
    assertEquals(
      privateAggregationContributions.custom_events[customEventName][0].bucket.bucket_128_bit.bucket_128_bits[0],
      10
    );
    assertEquals(
      privateAggregationContributions.custom_events[customEventName][0].bucket.bucket_128_bit.bucket_128_bits[1],
      0
    );
    assertEquals(privateAggregationContributions.custom_events[customEventName][0].value.int_value, 30);
  },

  /** @return {void} */
  testContributeToHistogram_InvalidNumericBucketAndValue() {
    // TODO(b/355693629): Uncomment invalid input cases when adding support for validation on contribution object.
    // const invalidContribution = 'invalid_contribution';
    // privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, invalidContribution);
    // assertEquals(testPwin.length, 1);
  },

  testContributeToHistogramOnEvent_InvalidCustomEvents() {
    const contribution = {
      bucket: 10,
      value: 30,
    };

    const invalidCustomEventName1 = 'reserved.';
    privateAggregation.contributeToHistogramOnEvent(invalidCustomEventName1, contribution);
    assertEquals(privateAggregationContributions.custom_events[invalidCustomEventName1], undefined);

    const invalidCustomEventName2 = '';
    privateAggregation.contributeToHistogramOnEvent(invalidCustomEventName2, contribution);
    assertEquals(privateAggregationContributions.custom_events[invalidCustomEventName2], undefined);

    const invalidCustomEventName3 = null;
    privateAggregation.contributeToHistogramOnEvent(invalidCustomEventName3, contribution);
    assertEquals(privateAggregationContributions.custom_events[invalidCustomEventName3], undefined);
  },

  /** @return {void} */
  testContributeToHistogramOnEvent_ValidObjectBucketAndValue() {
    const bucket_object = {
      baseValue: 'winning-bid',
      scale: 1.0,
      offset: 10,
    };
    const signal_bucket = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 1.0,
      offset: {
        value: [10, 0],
        is_negative: false,
      },
    };
    const value_object = {
      baseValue: 'winning-bid',
      scale: 1.0,
      offset: 30,
    };
    const signal_value = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 1.0,
      offset: 30,
    };
    const contribution = {
      bucket: bucket_object,
      value: value_object,
    };

    // Test with ReservedEventConstants
    privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, contribution);
    assertEquals(privateAggregationContributions.win.length, 1);
    // Assert the expected fields and values directly
    assertObjectEquals(privateAggregationContributions.win[0].bucket.signal_bucket, signal_bucket);
    assertObjectEquals(privateAggregationContributions.win[0].value.extended_value, signal_value);
  },

  /** @return {void} */
  testContributeToHistogramOnEvent_ValidObjectBucketAndNumericValue() {
    const contribution = {
      bucket: {
        baseValue: 'winning-bid',
        scale: 1.0,
        offset: 10,
      },
      value: 30,
    };
    const signal_bucket = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 1.0,
      offset: {
        value: [10, 0],
        is_negative: false,
      },
    };
    const customEventName = 'my_custom_event';

    // Test with ReservedEventConstants
    privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, contribution);
    // Test with custom event
    privateAggregation.contributeToHistogramOnEvent(customEventName, contribution);
    assertEquals(privateAggregationContributions.win.length, 1);
    // Assert the expected fields and values directly
    assertObjectEquals(privateAggregationContributions.win[0].bucket.signal_bucket, signal_bucket);
    assertEquals(privateAggregationContributions.win[0].value.int_value, 30);

    assertEquals(privateAggregationContributions.custom_events[customEventName].length, 1);
    // Assert the expected fields and values directly
    assertObjectEquals(
      privateAggregationContributions.custom_events[customEventName][0].bucket.signal_bucket,
      signal_bucket
    );
    assertEquals(privateAggregationContributions.custom_events[customEventName][0].value.int_value, 30);
  },

  /** @return {void} */
  testContributeToHistogramOnEvent_ValidNumericBucketAndObjectValue() {
    const contribution = {
      bucket: 5,
      value: {
        baseValue: 'winning-bid',
        scale: 10,
        offset: 20,
      },
    };
    const signalValue = {
      base_value: 'BASE_VALUE_WINNING_BID',
      scale: 10,
      offset: 20,
    };
    const customEventName = 'my_custom_event';

    // Test with ReservedEventConstants
    privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, contribution);
    assertEquals(privateAggregationContributions.win.length, 1);
    assertEquals(privateAggregationContributions.win[0].bucket.bucket_128_bit.bucket_128_bits[0], 5);
    assertEquals(privateAggregationContributions.win[0].bucket.bucket_128_bit.bucket_128_bits[1], 0);
    assertObjectEquals(privateAggregationContributions.win[0].value.extended_value, signalValue);

    // Test with custom event
    privateAggregation.contributeToHistogramOnEvent(customEventName, contribution);
    assertEquals(privateAggregationContributions.custom_events[customEventName].length, 1);
    // Assert the expected fields and values directly
    assertEquals(
      privateAggregationContributions.custom_events[customEventName][0].bucket.bucket_128_bit.bucket_128_bits[0],
      5
    );
    assertEquals(
      privateAggregationContributions.custom_events[customEventName][0].bucket.bucket_128_bit.bucket_128_bits[1],
      0
    );
    assertObjectEquals(
      privateAggregationContributions.custom_events[customEventName][0].value.extended_value,
      signalValue
    );
  },

  /** @return {void} */
  testPrivateAggregationContributions_Seal() {
    // Check if the object is sealed
    assertTrue(Object.isSealed(privateAggregationContributions));

    delete privateAggregationContributions.win;
    assertNotNullNorUndefined(privateAggregationContributions.win);
    assertArrayEquals([], privateAggregationContributions.win);
    delete privateAggregationContributions.custom_events;
    assertNotNullNorUndefined(privateAggregationContributions.custom_events);
    assertElementsEquals(new Map(), privateAggregationContributions.custom_events);
  },

  /** @return {void} */
  testContributeToHistogram() {
    const contribution = {
      bucket: 10,
      value: 30,
    };
    privateAggregation.contributeToHistogram(contribution);
    assertEquals(privateAggregationContributions.always.length, 1);
    // Assert the expected fields and values directly
    assertEquals(privateAggregationContributions.always[0].bucket.bucket_128_bit.bucket_128_bits[0], 10);
    assertEquals(privateAggregationContributions.always[0].bucket.bucket_128_bit.bucket_128_bits[1], 0);
    assertEquals(privateAggregationContributions.always[0].value.int_value, 30);

    const expectedErrorType = 'TypeError';
    const expectedErrorMessage = 'Attempting to define property on object that is not extensible.';
    const badField = 'new_field';
    const badValue = 'I should not be included.';

    try {
      Object.defineProperty(privateAggregationContributions, badField, {
        value: badValue,
      });
    } catch (e) {
      assertEquals(expectedErrorType, e.constructor.name);
      assertEquals(expectedErrorMessage, e.message);
    }

    privateAggregationContributions.badField = badValue;
    assertUndefined(privateAggregationContributions.badField);
  },
  /** @return {void} */
  tearDown() {
    privateAggregationContributions.win = [];
    privateAggregationContributions.loss = [];
    privateAggregationContributions.always = [];
    privateAggregationContributions.custom_events = {};
  },
});
