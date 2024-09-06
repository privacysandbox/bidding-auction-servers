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
 * @fileoverview JS file that implements and exposes bidding_service.privateAggregation
 */

/** @implements {PrivateAggregation} */
class BiddingServicePrivateAggregationImpl {
  /** Contributes to the histogram for a specific event.
   * This is the buyer's implementation of contributeToHistogramOnEvent API for PrivateAggregation.
   * This function has a side-effect of manipulating global variable private_aggregation_contributions.
   * @param {string} event_type_str is associated with the private aggregate contribution. This is expected to be one of the following : "reserved.win","reserved.loss","reserved.always" or a custom event name.
   * @param {*} pAggContribution is the contribution object
   * @override
   */
  contributeToHistogramOnEvent(event_type_str, pAggContribution) {
    const event = {
      event_type: biddingPrivateAggregationUtil.mapEventToEnum(event_type_str),
    };
    if (event.event_type == 'EVENT_TYPE_CUSTOM') {
      event.event_name = event_type_str;
    }

    const contribution = privateAggregationUtil.createContribution(pAggContribution.bucket, pAggContribution.value);

    contribution.event = event;

    private_aggregation_contributions.push(contribution);
  }

  /**
   * Contributes to the histogram for 'reserved.always' event.
   * @param {Object} pAggContribution The contribution object.
   *   This object should contain the bucket and value information.
   * @return {void}
   * @override
   */
  contributeToHistogram(pAggContribution) {
    privateAggregation.contributeToHistogramOnEvent('reserved.always', pAggContribution);
  }
}

/**
 * @type {?private_aggregation_contributions}
 * @const private_aggregation_contributions
 * @export
 */
const private_aggregation_contributions = [];
/**
 * @type {Object}
 * @export
 */
globalThis.ps_response = globalThis.ps_response || {};
globalThis.ps_response.paapicontributions = private_aggregation_contributions;
/**
 * @const {BiddingServicePrivateAggregationImpl}
 * @export
 */
const privateAggregation = new BiddingServicePrivateAggregationImpl();
