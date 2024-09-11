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
 * @fileoverview Private Aggregation Utility
 *
 * @externs
 */

/** @interface */
class PrivateAggregationUtil {
  /**
   * @param {number|bigint} value
   * @return {boolean}
   */
  isUnsigned128BitInteger(value) {}

  /**
   * @param {bigint} bucket
   * @return {?Array<number>}
   */
  convertTo128BitArray(bucket) {}
  /** Converts string baseValue type to equivalent ENUM BaseValue string.
   * @param {string} value
   * @return {string}
   * @throws {TypeError}
   * @override
   */
  convertBaseValueToEnumString(value) {}
  /**
   * Converts value object in contribution to SignalValue.
   *
   * @param {Object} value_obj The value object.
   * @return {{base_value: string, scale: number, offset: {value: Array<number>, is_negative: boolean}}} The SignalValue Object.
   * @override
   */
  convertToSignalValue(value_obj) {}

  /**
   * Converts bucket object in contribution to SignalBucket.
   *
   * @param {Object} bucket_obj The bucket object.
   * @return {{base_value: string, scale: number, offset: {value: Array<number>, is_negative: boolean}}} The SignalBucket Object.
   * @override
   */
  convertToSignalBucket(bucket_obj) {}
  /**
   * @param {number|bigint} offset
   * @return {{is_negative: boolean, value: ?Array<number>}}
   */
  convertToBucketOffset(offset) {}

  /**
   * @param {number|Object} bucket
   * @return {boolean}
   */
  isValidBucket(bucket) {}

  /**
   * @param {number|Object} value
   * @return {boolean}
   */
  isValidValue(value) {}

  /*
   * @param {string} event_type
   * @return {boolean}
   */
  isValidCustomEvent(event_type) {}

  /**
   * @param {Object} contribution
   * @return {boolean}
   */
  isValidContribution(contribution) {}

  /**
   * TODO(b/355034881): All usage of "number" type for bucket should be replaced as "bigint" once bigint support is enabled.
   */
  /**
   * @param {Object | number | *} bucket
   * @param {Object | number | *} value
   * @return {Object | null} contribution
   */
  createContribution(bucket, value) {}
}
