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
/**
 * @fileoverview JS file that implements and exposes privateAggregation
 */

/** @implements {BiddingPrivateAggregationUtil} */
class BiddingPrivateAggregationUtilImpl {
  /** Converts string event type to an int using the values from enum EventType.
   * @param {string} event
   * @return {string}
   * @throws {TypeError}
   * @override
   */
  mapEventToEnum(event) {
    if (event == null) {
      throw new TypeError('Event type cannot be null.');
    }
    if (typeof event !== 'string') {
      throw new TypeError('Event type must be string.');
    }
    switch (event) {
      case 'reserved.win':
        return 'EVENT_TYPE_WIN';
      case 'reserved.loss':
        return 'EVENT_TYPE_LOSS';
      case 'reserved.always':
        return 'EVENT_TYPE_ALWAYS';
      default:
        if (event.startsWith('reserved.')) {
          throw new TypeError('Event type cannot begin with reserved.');
        }
        return 'EVENT_TYPE_CUSTOM'; // Custom event
    }
  }
}

/**
 * @const {BiddingPrivateAggregationUtilImpl}
 * @export
 */
const biddingPrivateAggregationUtil = new BiddingPrivateAggregationUtilImpl();
// Make privateAggregationUtil properties and methods immutable.
Object.freeze(biddingPrivateAggregationUtil);
