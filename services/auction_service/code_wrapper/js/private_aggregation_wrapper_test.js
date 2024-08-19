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

    const customEventName = 'my_custom_event';

    // Test with ReservedEventConstants
    const reservedWin = 'reserved.win';
    assertEquals(ReservedEventConstants.WIN, reservedWin);
    privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, contribution);
    assertEquals(privateAggregationContributions.win.length, 1);
    // Assert the expected fields and values directly
    assertEquals(privateAggregationContributions.win[0].bucket_128_bit, 10);
    assertEquals(privateAggregationContributions.win[0].int_value, 30);

    // Test with custom event
    privateAggregation.contributeToHistogramOnEvent(customEventName, contribution);
    assertEquals(privateAggregationContributions.custom_events.get(customEventName).length, 1);
    // Assert the expected fields and values directly
    assertEquals(privateAggregationContributions.custom_events.get(customEventName)[0].bucket_128_bit, 10);
    assertEquals(privateAggregationContributions.custom_events.get(customEventName)[0].int_value, 30);
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
    assertEquals(privateAggregationContributions.custom_events.get(invalidCustomEventName1), undefined);

    const invalidCustomEventName2 = '';
    privateAggregation.contributeToHistogramOnEvent(invalidCustomEventName2, contribution);
    assertEquals(privateAggregationContributions.custom_events.get(invalidCustomEventName2), undefined);

    const invalidCustomEventName3 = null;
    privateAggregation.contributeToHistogramOnEvent(invalidCustomEventName3, contribution);
    assertEquals(privateAggregationContributions.custom_events.get(invalidCustomEventName3), undefined);
  },

  /** @return {void} */
  testContributeToHistogramOnEvent_ValidObjectBucketAndValue() {
    const bucket_object = {
      base_value: 'winning-bid',
      scale: 1.0,
      offset: {
        value: [10, 20],
        is_negative: false,
      },
    };

    const value_object = {
      base_value: 'winning-bid',
      offset: 30,
      scale: 1.0,
    };
    const contribution = {
      bucket: bucket_object,
      value: value_object,
    };

    const customEventName = 'my_custom_event';

    // Test with ReservedEventConstants
    privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, contribution);
    assertEquals(privateAggregationContributions.win.length, 1);
    // Assert the expected fields and values directly
    assertObjectEquals(privateAggregationContributions.win[0].signal_bucket, bucket_object);
    assertObjectEquals(privateAggregationContributions.win[0].extended_value, value_object);

    // Test with custom event
    privateAggregation.contributeToHistogramOnEvent(customEventName, contribution);
    assertEquals(privateAggregationContributions.custom_events.get(customEventName).length, 1);
    // Assert the expected fields and values directly
    assertObjectEquals(
      privateAggregationContributions.custom_events.get(customEventName)[0].signal_bucket,
      bucket_object
    );
    assertObjectEquals(
      privateAggregationContributions.custom_events.get(customEventName)[0].extended_value,
      value_object
    );
  },

  /** @return {void} */
  testContributeToHistogramOnEvent_ValidObjectBucketAndNumericValue() {
    const contribution = {
      bucket: {
        base_value: 'winning-bid',
        scale: 1.0,
        offset: {
          value: [10, 20],
          is_negative: false,
        },
      },
      value: 30,
    };
    const customEventName = 'my_custom_event';

    // Test with ReservedEventConstants
    privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, contribution);
    assertEquals(privateAggregationContributions.win.length, 1);
    // Assert the expected fields and values directly
    assertObjectEquals(privateAggregationContributions.win[0].signal_bucket, contribution.bucket);
    assertEquals(privateAggregationContributions.win[0].int_value, 30);

    // Test with custom event
    privateAggregation.contributeToHistogramOnEvent(customEventName, contribution);
    assertEquals(privateAggregationContributions.custom_events.get(customEventName).length, 1);
    // Assert the expected fields and values directly
    assertObjectEquals(
      privateAggregationContributions.custom_events.get(customEventName)[0].signal_bucket,
      contribution.bucket
    );
    assertEquals(privateAggregationContributions.custom_events.get(customEventName)[0].int_value, 30);
  },

  /** @return {void} */
  testContributeToHistogramOnEvent_ValidNumericBucketAndObjectValue() {
    const contribution = {
      bucket: 5,
      value: {
        base_value: 'winning-bid',
        value: 30,
        is_negative: false,
      },
    };

    const customEventName = 'my_custom_event';

    // Test with ReservedEventConstants
    privateAggregation.contributeToHistogramOnEvent(ReservedEventConstants.WIN, contribution);
    assertEquals(privateAggregationContributions.win.length, 1);
    assertEquals(privateAggregationContributions.win[0].bucket_128_bit, 5);
    assertObjectEquals(privateAggregationContributions.win[0].extended_value, contribution.value);

    // Test with custom event
    privateAggregation.contributeToHistogramOnEvent(customEventName, contribution);
    assertEquals(privateAggregationContributions.custom_events.get(customEventName).length, 1);
    // Assert the expected fields and values directly
    assertEquals(privateAggregationContributions.custom_events.get(customEventName)[0].bucket_128_bit, 5);
    assertObjectEquals(
      privateAggregationContributions.custom_events.get(customEventName)[0].extended_value,
      contribution.value
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
    assertEquals(privateAggregationContributions.always[0].bucket_128_bit, 10);
    assertEquals(privateAggregationContributions.always[0].int_value, 30);

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
    privateAggregationContributions.custom_events.clear();
  },
});
