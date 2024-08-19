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

goog.module('bidding_service.privateAggregationUtil.test');
const testSuite = goog.require('goog.testing.testSuite');

testSuite({
  /** @return {void} */
  /** @suppress {reportUnknownTypes, checkTypes} */
  testConvertEventToInt() {
    // Test reserved events
    var result = new BiddingPrivateAggregationUtilImpl().mapEventToEnum('reserved.win');
    assertEquals(result, 'EVENT_TYPE_WIN');
    result = new BiddingPrivateAggregationUtilImpl().mapEventToEnum('reserved.always');
    assertEquals(result, 'EVENT_TYPE_ALWAYS');

    // Test custom event
    result = new BiddingPrivateAggregationUtilImpl().mapEventToEnum('user-interaction');
    assertEquals(result, 'EVENT_TYPE_CUSTOM');

    // Test invalid event starting with 'reserved.'
    try {
      new BiddingPrivateAggregationUtilImpl().mapEventToEnum('reserved.notARealEvent');
      fail('Expected TypeError for invalid reserved event');
    } catch (error) {
      assertTrue(error instanceof TypeError);
    }

    // Test int (invalid) event
    try {
      new BiddingPrivateAggregationUtilImpl().mapEventToEnum(1);
      fail('Expected TypeError for null event');
    } catch (error) {
      assertTrue(error instanceof TypeError);
    }

    // Test null event
    try {
      new BiddingPrivateAggregationUtilImpl().mapEventToEnum(null);
      fail('Expected TypeError for null event');
    } catch (error) {
      assertTrue(error instanceof TypeError);
    }
  },
  /** @return {void} */
  /** @suppress {reportUnknownTypes, checkTypes} */
  testConvertBaseValueToInt() {
    // Test defined base values
    var result = new BiddingPrivateAggregationUtilImpl().convertBaseValueToInt('winning-bid');
    assertEquals(result, 0);
    result = new BiddingPrivateAggregationUtilImpl().convertBaseValueToInt('highest-scoring-other-bid');
    assertEquals(result, 1);

    // Test TBD cases (assuming these return values are defined later)
    result = new BiddingPrivateAggregationUtilImpl().convertBaseValueToInt('script-run-time');
    assertEquals(result, 3);
    result = new BiddingPrivateAggregationUtilImpl().convertBaseValueToInt('signals-fetch-time');
    assertEquals(result, 4);

    // Test unsupported base value type
    try {
      new BiddingPrivateAggregationUtilImpl().convertBaseValueToInt('something-else');
      fail('Expected TypeError for unsupported base value');
    } catch (error) {
      assertTrue(error instanceof TypeError);
    }

    // Test int (invalid) base value
    try {
      new BiddingPrivateAggregationUtilImpl().convertBaseValueToInt(1);
      fail('Expected TypeError for null base value');
    } catch (error) {
      assertTrue(error instanceof TypeError);
    }

    // Test null base value
    try {
      new BiddingPrivateAggregationUtilImpl().convertBaseValueToInt(null);
      fail('Expected TypeError for null base value');
    } catch (error) {
      assertTrue(error instanceof TypeError);
    }
  },
});
