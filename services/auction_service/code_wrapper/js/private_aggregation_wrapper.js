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
 * @fileoverview JS file that implements and exposes auction_service.privateAggregation
 */

/** @implements {PrivateAggregation} */
class AuctionServicePrivateAggregationImpl {
  /**
   * Contributes to the histogram for a specific event.
   * If event is null/empty/starts with 'reserved'. , contribution will be dropped and no error will be thrown
   *
   * @param {string | Object} event The event type or object.
   *   If it's a string, it should be a valid custom event name.
   *   If it's an object, it should be an instance of EventType.
   * @param {Object} contribution The contribution object.
   * This object should contain the bucket and value information.
   * - Example contribution `{bucket: 10, value: 10}`
   * @return {void}
   */
  contributeToHistogramOnEvent(event, contribution) {
    // Type-check and create a contribution object.
    const newContribution = privateAggregationUtil.createContribution(contribution.bucket, contribution.value);
    switch (event) {
      case ReservedEventConstants.WIN:
        privateAggregationContributions.win.push(newContribution);
        break;
      case ReservedEventConstants.LOSS:
        privateAggregationContributions.loss.push(newContribution);
        break;
      case ReservedEventConstants.ALWAYS:
        privateAggregationContributions.always.push(newContribution);
        break;
      default:
        if (event === null || event === '' || event.startsWith('reserved.')) {
          return;
        }
        // Initialize custom_events as an empty object if it doesn't exist
        privateAggregationContributions.custom_events = privateAggregationContributions.custom_events || {};
        // Access and update the array for the given event
        privateAggregationContributions.custom_events[event] =
          privateAggregationContributions.custom_events[event] || [];
        privateAggregationContributions.custom_events[event].push(newContribution);
    }
  }

  /**
   * Contributes to the histogram for 'reserved.always' event.
   *
   * @param {Object} contribution The contribution object.
   *   This object should contain the bucket and value information.
   * @return {void}
   */
  contributeToHistogram(contribution) {
    // TODO(b/355693629): Add typecheck for contribution object.
    this.contributeToHistogramOnEvent(ReservedEventConstants.ALWAYS, contribution);
  }
}

/**
 * @const {AuctionServicePrivateAggregationImpl}
 * @export
 */
const privateAggregation = new AuctionServicePrivateAggregationImpl();

/**
 * @typedef {Object} privateAggregationContributions
 * @property {?Array<Object>} win - An array of contributions for the 'reserved.win' event.
 * @property {?Array<Object>} loss - An array of contributions for the 'reserved.loss' event.
 * @property {?Array<Object>} always - An array of contributions for the 'reserved.always' event.
 * @property {?Map<string, Array<Object>>} custom_events - A map of custom event names to their corresponding contributions.
 */
const privateAggregationContributions = { win: [], loss: [], always: [], custom_events: {} };
Object.seal(privateAggregationContributions);

/** @type {Object} */
globalThis.ps_response = globalThis.ps_response || {};

/** @type {?privateAggregationContributions}
 * @export
 */
globalThis.ps_response.paapiContributions = privateAggregationContributions;
