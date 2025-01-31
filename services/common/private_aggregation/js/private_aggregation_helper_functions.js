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

/** @implements {PrivateAggregationUtil}
 * @suppress {checkTypes}
 * */
class PrivateAggregationUtilImpl {
  /**
   * Checks if the number is an 128-bit unsigned integer.
   * @param {bigint} number The number to check.
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
   * @param {bigint} bucket The 128-bit unsigned integer to convert.
   * @return {Array<number>} An array of two 64-bit integers representing the 128-bit integer.
   * The first element of the array is the high 64 bits of the 128-bit integer, and the second
   * element is the low 64 bits of the 128-bit integer.
   * @throws {TypeError} If bucket type is unrecognized.
   * @override
   */
  convertTo128BitArray(bucket) {
    if (typeof bucket === 'number') {
      const highBits = bucket >>> 64; // Right shift 64 bits (zero-fill)
      const lowBits = bucket & 0xffffffffffffffff; // Mask to get the lower 64 bits
      return [highBits, lowBits];
    } else if (typeof bucket === 'bigint') {
      // Get the high and low bits as BigInts
      const highBits = bucket >> BigInt(64);
      const lowBits = bucket & BigInt('0xffffffffffffffff');
      // Convert the BigInts back to regular numbers
      const highBitsNumber = Number(highBits);
      const lowBitsNumber = Number(lowBits);
      return [highBitsNumber, lowBitsNumber];
    } else {
      throw new TypeError('bucket type not supported.');
    }
  }
  /**
   * Converts numerical offset to BucketOffset.
   *
   * @param {bigint} offset The numerical offset to convert.
   * @return {{value: Array<number>, is_negative: boolean}} The BucketOffset Object.
   * @override
   */
  convertToBucketOffset(offset) {
    return {
      value: this.convertTo128BitArray(Math.abs(offset)),
      is_negative: offset < 0,
    };
  }

  /** Converts string baseValue type to equivalent ENUM BaseValue string.
   * @param {string} value
   * @return {string}
   * @throws {TypeError}
   * @override
   */
  convertBaseValueToEnumString(value) {
    switch (value) {
      case 'winning-bid':
        return 'BASE_VALUE_WINNING_BID';
      case 'highest-scoring-other-bid':
        return 'BASE_VALUE_HIGHEST_SCORING_OTHER_BID';
      case 'bid-rejection-reason':
        return 'BASE_VALUE_BID_REJECTION_REASON';
      //TBD
      case 'script-run-time':
        return 'BASE_VALUE_SCRIPT_RUN_TIME';
      //TBD
      case 'signals-fetch-time':
        return 'BASE_VALUE_SIGNALS_FETCH_TIME';
      default:
        throw new TypeError('Base value type not supported.');
    }
  }

  /**
   * Converts bucket object in contribution to SignalBucket.
   *
   * @param {Object} bucket_obj The bucket object.
   * @return {{base_value: string, scale: number, offset: {value: Array<number>, is_negative: boolean}}} The SignalBucket Object.
   * @override
   */
  convertToSignalBucket(bucket_obj) {
    var scale = 1.0;
    var offset = this.convertToBucketOffset(0);
    // Optional scale factor for the bucket. Default value will be 1.0
    if (bucket_obj.scale) {
      scale = bucket_obj.scale;
    }
    // Optional offset for the bucket. Default value will be 0
    if (bucket_obj.offset) {
      offset = this.convertToBucketOffset(bucket_obj.offset);
    }
    return {
      base_value: this.convertBaseValueToEnumString(bucket_obj.baseValue),
      scale: scale,
      offset: offset,
    };
  }

  /**
   * Converts value object in contribution to SignalValue.
   *
   * @param {Object} value_obj The value object.
   * @return {{base_value: string, scale: number, offset: number}} The SignalValue Object.
   * @override
   */
  convertToSignalValue(value_obj) {
    var scale = 1.0;
    var offset = 0;
    // Optional scale factor for the value. Default value will be 1.0
    if (value_obj.scale) {
      scale = value_obj.scale;
    }
    // Optional offset for the value. Default value will be 0
    if (value_obj.offset) {
      offset = value_obj.offset;
    }
    return {
      base_value: this.convertBaseValueToEnumString(value_obj.baseValue),
      scale: scale,
      offset: offset,
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
      if (value.baseValue != null && Object.values(BaseValue).includes(value.baseValue)) {
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
     * @property {?Object} bucket
     * @property {?Object} value
     */
    const contribution = {
      bucket: {},
      value: {},
    };
    if (typeof bucket === 'object') {
      contribution.bucket.signal_bucket = this.convertToSignalBucket(bucket);
    } else if (typeof bucket === 'string') {
      throw new TypeError('Invalid type for Private Aggregation bucket.');
    } else {
      contribution.bucket.bucket_128_bit = {};
      contribution.bucket.bucket_128_bit.bucket_128_bits = privateAggregationUtil.convertTo128BitArray(bucket);
    }
    if (typeof value === 'number') {
      contribution.value.int_value = value;
    } else if (typeof value === 'object') {
      contribution.value.extended_value = privateAggregationUtil.convertToSignalValue(value);
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
