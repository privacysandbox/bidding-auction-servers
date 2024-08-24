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
/**
 * @fileoverview JS file that implements and exposes privateAggregation
 */

/**
 * @typedef {Object} BaseValue
 * @property {string} BASE_VALUE_UNKNOWN
 * @property {string} WINNING_BID
 * @property {string} HIGHEST_SCORING_OTHER_BID
 * @property {string} SCRIPT_RUN_TIME
 * @property {string} SIGNALS_FETCH_TIME
 * @property {string} BID_REJECTION_REASON
 */
const BaseValue = {
  BASE_VALUE_UNKNOWN: 'base-value-unknown',
  WINNING_BID: 'winning-bid',
  HIGHEST_SCORING_OTHER_BID: 'highest-scoring-other-bid',
  SCRIPT_RUN_TIME: 'script-run-time',
  SIGNALS_FETCH_TIME: 'signals-fetch-time',
  BID_REJECTION_REASON: 'bid-rejection-reason',
};

const ReservedEventConstants = Object.freeze({
  WIN: 'reserved.win',
  LOSS: 'reserved.loss',
  ALWAYS: 'reserved.always',
});

// Make properties of BaseValue and ReservedEventConstants immutable.
Object.freeze(BaseValue);
Object.freeze(ReservedEventConstants);

/** @implements {PrivateAggregationUtil} */
class PrivateAggregationUtilImpl {
  /**
   * Checks if the number is an 128-bit unsigned integer.
   * @param {number|bigint} number The number to check.
   * @return {boolean} True if the number is an unsigned 128-bit integer, false otherwise.
   *
   * @override
   */
  isUnsigned128BitInteger(number) {
    return false;
  }
  /**
   * Converts a 128-bit unsigned integer to an array of two 64-bit integers.
   *
   * @param {number|bigint} number The 128-bit unsigned integer to convert.
   * @return {Array<number>} An array of two 64-bit integers representing the 128-bit integer.
   * @override
   */
  convertTo128BitArray(number) {
    return null;
  }
  /**
   * Converts numerical offset to BucketOffset.
   *
   * @param {number|bigint} offset The numerical offset to convert.
   * @return {{value: Array<number>, is_negative: boolean}} The BucketOffset Object.
   * @override
   */
  convertToBucketOffset(offset) {
    return {
      value: null,
      is_negative: false,
    };
  }

  /**
   * Checks if the bucket is valid.
   *
   * @param {number|Object} bucket The bucket to check.
   * @return {boolean} True if the bucket is valid, false otherwise.
   * @override
   */
  isValidBucket(bucket) {
    return false;
  }

  /**
   * Checks if the value is a valid unsigned 32-bit integer.
   *
   * @param {number|Object} value The PrivateAggregation contribution value to check.
   * @return {boolean} True if the PrivateAggregation contribution value is valid, false otherwise.
   * @override
   * @throws {RangeError} If the PrivateAggregation contribution value is not a valid Unsigned 32-bit Integer.
   */
  isValidValue(value) {
    if (typeof value === 'number') {
      if (value < 0 || value >= 2 ** 32 - 1) {
        throw new RangeError('Value must be an unsigned 32-bit integer.');
      }
      return true;
    } else if (typeof value === 'object') {
      // Type-check optional values.
      if (
        (value.scale != null && typeof value.scale != 'number') ||
        (value.offset != null && typeof value.offset != 'number')
      ) {
        return false;
      }
      // Check if it contains the required base_value field.
      if (value.base_value != null && Object.values(BaseValue).includes(value.base_value)) {
        return true;
      }
    }
    return false;
  }

  /**
   * Checks if the event type is valid.
   *
   * @param {string} event_type The event type to check.
   * @return {boolean} True if the event type is valid, false otherwise.
   * @override
   */
  isValidCustomEvent(event_type) {
    return typeof event_type === 'string' && !event_type.startsWith('reserved.');
  }

  /**
   * Checks if the contribution is valid.
   *
   * @param {Object} contribution The contribution to check.
   * @return {boolean} True if the contribution is valid, false otherwise.
   * @override
   */
  isValidContribution(contribution) {
    return false;
  }

  /**
   * TODO(b/355034881): All usage of "number" type for bucket should be replaced as "bigint" once bigint support is enabled.
   */
  /**
   * Create a Private Aggregation contribution object with the input Private Aggregation bucket and value.
   *
   * @param {Object | number | *} bucket
   * Example:
   * - `bigint` example: `10n`
   * - `Object` type assumes `SignalBucket` Object to be passed in:
   *   - `SignalBucket` example: `{base_value: 'winning-bid', scale: 1.0, offset: BucketOffset object}`.
   *   - `BucketOffset` example: `{value: [5, 0], is_negative: false}`
   * @param {Object | number | *} value
   * - `value` of `number` type should be within the range `[0, 2^32-1]`.
   *   - Example: `5`
   * - `Object` type assumes `SignalValue` Object to be passed in:
   *   - `SignalBucket` example: `{base_value: 'winning-bid', scale: 1.0}`.
   * @return {Object | null} contribution. `null` is returned if either bucket or value is invalid.
   * @throws {TypeError} If either input Private Aggregation bucket or value is invalid type.
   * @throws {RangeError} If Private Aggregation value of `number` type is not within the range `[0, 2^32-1]` // TODO(b/355034881): RangeError should be catched from isValidValue.
   */
  createContribution(bucket, value) {
    // TODO(b/355034881): Add validation of SignalBucket and SignalValue's fields.
    /**
     * @const {?Object}
     * @property {?number} bucket_128_bit
     * @property {?Object} signal_bucket
     * @property {?number} int_value
     * @property {?Object} extended_value
     */
    const contribution = {
      bucket_128_bit: null,
      signal_bucket: null,
      int_value: null,
      extended_value: null,
    };

    if (typeof bucket === 'number') {
      contribution.bucket_128_bit = bucket; // TODO(b/355034881): Once bigint is supported, should be Array returned from PrivateAggregationUtilImpl.prototype.convertTo128BitArray(bucket);
    } else if (typeof bucket === 'object') {
      contribution.signal_bucket = bucket;
    } else {
      throw new TypeError('Invalid type for Private Aggregation bucket.');
    }

    if (typeof value === 'number') {
      contribution.int_value = value;
    } else if (typeof value === 'object') {
      contribution.extended_value = value;
    } else {
      throw new TypeError('Invalid type for Private Aggregation value.');
    }

    return contribution;
  }
}

/**
 * @const {PrivateAggregationUtilImpl}
 * @export
 */
const privateAggregationUtil = new PrivateAggregationUtilImpl();
// Make privateAggregationUtil properties and methods immutable.
Object.freeze(privateAggregationUtil);
