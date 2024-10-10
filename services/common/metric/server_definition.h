//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef SERVICES_COMMON_METRIC_SERVER_DEFINITION_H_
#define SERVICES_COMMON_METRIC_SERVER_DEFINITION_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "services/common/loggers/request_log_context.h"
#include "services/common/metric/error_code.h"
#include "services/common/util/read_system.h"
#include "services/common/util/reporting_util.h"
#include "src/metric/context_map.h"
#include "src/metric/definition.h"
#include "src/metric/key_fetch.h"
#include "utils/inference_error_code.h"

// Defines API used by Bidding auction servers, and B&A specific metrics.
namespace privacy_sandbox::bidding_auction_servers {
namespace metric {

inline constexpr server_common::metrics::PrivacyBudget kServerTotalBudget{
    /*epsilon*/ 10};

inline constexpr double kPercentHistogram[] = {
    0.0078125, 0.015625, 0.03125, 0.0625, 0.125, 0.25, 0.5, 1};

inline constexpr double kCountHistogram[] = {2,  5,   10,  20,  40,
                                             80, 160, 320, 640, 1'280};

inline constexpr absl::string_view kAs = "AS";
inline constexpr absl::string_view kBs = "BS";
inline constexpr absl::string_view kKv = "KV";
inline constexpr absl::string_view kBfe = "BFE";
inline constexpr absl::string_view kSfe = "SFE";

inline constexpr absl::string_view kServerName[]{
    kAs, kBfe, kBs, kKv, kSfe,
};

constexpr int kMaxBuyersSolicited = 2;

// Metrics with dynamic partitions cannot have empty static partitions.
inline constexpr std::array<std::string_view, 1> kDefaultDynamicPartition = {
    "default"};

// Metric Definitions that are specific to bidding & auction servers.

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kProtectedCiphertextSize("sfe.protected_ciphertext.size_bytes",
                             "Size of protected ciphertext in Bytes",
                             server_common::metrics::kSizeHistogram);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kAuctionConfigSize("sfe.auction_config.size_bytes",
                       "Size of auction config in Bytes",
                       server_common::metrics::kSizeHistogram);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kJSExecutionDuration("js_execution.duration_ms",
                         "Time taken to execute the JS dispatcher",
                         server_common::metrics::kTimeHistogram, 300, 10);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kPASGenerateBidJSExecutionDuration(
        "pas_generate_bid_js_execution.duration_ms",
        "Time taken to execute the PAS GenerateBid JS dispatcher",
        server_common::metrics::kTimeHistogram, 300, 10);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kPASPrepareDataForRetrievalJSExecutionDuration(
        "pas_prepare_data_for_retrieval_js_execution.duration_ms",
        "Time taken to execute the PAS PrepareDataForAdRetrieval JS dispatcher",
        server_common::metrics::kTimeHistogram, 300, 10);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kJSExecutionErrorCount("js_execution.errors_count",
                           "No. of times js execution returned status != OK", 1,
                           0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestKVDuration(
        "initiated_request.to_kv.duration_ms",
        "Total duration request takes to get response back from KV server",
        server_common::metrics::kTimeHistogram, 1'000, 10);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestBiddingDuration(
        "bfe.initiated_request.to_bidding.duration_ms",
        "Total duration request takes to get response back from bidding server",
        server_common::metrics::kTimeHistogram, 1'000, 10);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestAuctionDuration(
        "sfe.initiated_request.to_auction.duration_ms",
        "Total duration request takes to get response back from Auction server",
        server_common::metrics::kTimeHistogram, 1'000, 10);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestKVSize(
        "initiated_request.to_kv.size_bytes",
        "Size of the Initiated Request to KV server in Bytes",
        server_common::metrics::kSizeHistogram, 25'000, 100);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestBiddingSize(
        "bfe.initiated_request.to_bidding.size_bytes",
        "Size of the Initiated Request to Bidding server in Bytes",
        server_common::metrics::kSizeHistogram, 5'000'000, 5'000);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestAuctionSize(
        "sfe.initiated_request.to_auction.size_bytes",
        "Size of the initiated Request to Auction server in Bytes",
        server_common::metrics::kSizeHistogram, 1'000'000, 1'000);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kBfeInitiatedResponseKVSize(
        "bfe.initiated_response.to_kv.size_bytes",
        "Size of the Initiated Response by BFE KV server in Bytes",
        server_common::metrics::kSizeHistogram, 5'000'000, 5'000);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kSfeInitiatedResponseKVSize(
        "sfe.initiated_response.to_kv.size_bytes",
        "Size of the Initiated Response by SFE KV server in Bytes",
        server_common::metrics::kSizeHistogram, 100'000, 1'000);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedResponseBiddingSize(
        "bfe.initiated_response.to_bidding.size_bytes",
        "Size of the Initiated Response by Bidding server in Bytes",
        server_common::metrics::kSizeHistogram, 100'000, 1'000);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedResponseAuctionSize(
        "sfe.initiated_response.to_auction.size_bytes",
        "Size of the initiated Response by Auction server in Bytes",
        server_common::metrics::kSizeHistogram, 100'000, 1'000);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInitiatedRequestCountByServer(
        /*name*/ "initiated_request.count",
        /*description*/
        "Total number of requests initiated by the server partitioned by "
        "outgoing server",
        /*partition_type*/ "server name",
        /*max_partitions_contributed*/ 3,
        /* public_partitions*/ kServerName,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kBiddingZeroBidCount(
        "bidding.business_logic.zero_bid_count",
        "Total number of times bidding service returns a zero bid", 15, 0);

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kBiddingZeroBidPercent(
        "bidding.business_logic.zero_bid_percent",
        "Percentage of times bidding service returns a zero bid",
        kPercentHistogram, 1, 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kBiddingInferenceRequestDuration(
        "bidding.inference.request.duration_ms",
        "Time taken by Roma callback to execute inference",
        server_common::metrics::kTimeHistogram, 300, 0);

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kGauge>
    kInferenceCloudFetchSuccessCount(
        "inference.cloud_fetch.success_count",
        "Success count for fetching model blobs from the cloud bucket");

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kGauge>
    kInferenceCloudFetchFailedCountByStatus(
        "inference.cloud_fetch.failed_count_by_status",
        "Failed count for fetching model blobs from the cloud bucket by "
        "status");

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kGauge>
    kInferenceRecentModelRegistrationSuccess(
        "inference.model_registration.recent_success_by_model",
        "Success count for registering models with inference sidecar by model "
        "in the most recent fetch");

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kGauge>
    kInferenceModelRegistrationFailedCountByStatus(
        "inference.model_registration.failed_count_by_status",
        "Failed count for registering models with inference sidecar by status");

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kGauge>
    kInferenceRecentModelRegistrationFailure(
        "inference.model_registration.recent_failure_by_model",
        "Failed count for registering models with inference sidecar by model "
        "in the most recent fetch");

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kGauge>
    kInferenceAvailableModels("inference.model_registration.available_models",
                              "List of all currently available models");

inline constexpr absl::string_view kSellerRejectReasons[] = {
    kRejectionReasonBidBelowAuctionFloor,
    kRejectionReasonBidFromGenBidFailedCurrencyCheck,
    kRejectionReasonBidFromScoreAdFailedCurrencyCheck,
    kRejectionReasonBlockedByPublisher,
    kRejectionReasonCategoryExclusions,
    kRejectionReasonDisapprovedByExchange,
    kRejectionReasonInvalidBid,
    kRejectionReasonLanguageExclusions,
    kRejectionReasonNotAvailable,
    kRejectionReasonPendingApprovalByExchange,
};

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kAuctionBidRejectedCount(
        /*name*/ "auction.business_logic.bid_rejected_count",
        /*description*/
        "Total number of times auction service rejects a bid partitioned by the"
        "seller rejection reason",
        /*partition_type*/ "seller_rejection_reason",
        /*max_partitions_contributed*/ 3,
        /*public_partitions*/ kSellerRejectReasons,
        /*upper_bound*/ 3,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kAuctionBidRejectedPercent(
        /*name*/ "auction.business_logic.bid_rejected_percent",
        /*description*/
        "Percentage of times auction service rejects a bid", kPercentHistogram,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kBiddingTotalBidsCount(
        /*name*/ "bidding.business_logic.bids_count",
        /*description*/ "Total number of bids generated by bidding service",
        /*upper_bound*/ 100,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kGauge>
    kThreadCount("system.thread.count", "Thread Count");

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kAuctionTotalBidsCount(
        /*name*/ "auction.business_logic.bids_count",
        /*description*/
        "Total number of bids used to score in auction service",
        /*upper_bound*/ 100,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kRequestWithWinnerCount(
        /*name*/ "sfe.business_logic.request_with_winner_count",
        /*description*/
        "Total number of SFE request with winner ads", 1, 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kSfeWithWinnerTimeMs(
        /*name*/ "sfe.business_logic.request_with_winner.duration_ms",
        /*description*/
        "Total time taken by SFE to execute the request with winner ads",
        server_common::metrics::kTimeHistogram, 1000, 100);

// For error_code metrics, min_noise_to_output = 0.99, noise bound = 8.29,
// privacy budget = 0.55
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kAuctionErrorCountByErrorCode(
        /*name*/ "auction.errors_count",
        /*description*/
        "Number of errors in the auction server by error code",
        /*partition_type*/ "error_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ kAuctionErrorCode,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0,
        /*min_noise_to_output*/ 0.99);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kBfeErrorCountByErrorCode(
        /*name*/ "bfe.errors_count",
        /*description*/
        "Number of errors in the BFE server by error code",
        /*partition_type*/ "error_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ kBfeErrorCode,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0,
        /*min_noise_to_output*/ 0.99);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kBiddingErrorCountByErrorCode(
        /*name*/ "bidding.errors_count",
        /*description*/
        "Number of errors in the bidding server by error code",
        /*partition_type*/ "error_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ kBiddingErrorCode,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0,
        /*min_noise_to_output*/ 0.99);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeErrorCountByErrorCode(
        /*name*/ "sfe.errors_count",
        /*description*/
        "Number of errors in the SFE server by error code",
        /*partition_type*/ "error_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ kSfeErrorCode,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0,
        /*min_noise_to_output*/ 0.99);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInferenceErrorCountByErrorCode(
        /*name*/ "inference.errors_count",
        /*description*/
        "Number of errors in the inference sidecar by error code",
        /*partition_type*/ "error_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ inference::kInferenceErrorCode,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0,
        /*min_noise_to_output*/ 0.99);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedRequestErrorsCountByBuyer(
        /*name*/ "sfe.initiated_request.to_bfe.errors_count_by_buyer",
        /*description*/
        "Total number of initiated requests failed per buyer",
        /*partition_type*/ "buyer",
        /*max_partitions_contributed*/ kMaxBuyersSolicited,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedRequestCountByBuyer(
        /*name*/ "sfe.initiated_request.to_bfe.count",
        /*description*/
        "Total number of initiated requests per buyer",
        /*partition_type*/ "buyer",
        /*max_partitions_contributed*/ kMaxBuyersSolicited,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedRequestDurationByBuyer(
        /*name*/ "sfe.initiated_request.to_bfe.duration_ms",
        /*description*/
        "Initiated requests duration  per buyer",
        /*partition_type*/ "buyer",
        /*max_partitions_contributed*/ kMaxBuyersSolicited,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 2'000,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedRequestSizeByBuyer(
        /*name*/ "sfe.initiated_request.to_bfe.size_bytes",
        /*description*/
        "Initiated requests size  per buyer",
        /*partition_type*/ "buyer",
        /*max_partitions_contributed*/ kMaxBuyersSolicited,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 500'000,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedResponseSizeByBuyer(
        /*name*/ "sfe.initiated_response.to_bfe.size_bytes",
        /*description*/
        "Initiated response size  per buyer",
        /*partition_type*/ "buyer",
        /*max_partitions_contributed*/ kMaxBuyersSolicited,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 30'000,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kRequestFailedCountByStatus(
        /*name*/ "request.failed_count",
        /*description*/
        "Total number of requests that resulted in failure partitioned by "
        "status code",
        /*partition_type*/ "status_code",
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInitiatedRequestKVErrorCountByStatus(
        /*name*/ "initiated_request.to_kv.errors_count",
        /*description*/
        "Initiated requests by KV that resulted in failure partitioned by "
        "status code",
        /*partition_type*/ "status_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInitiatedRequestAuctionErrorCountByStatus(
        /*name*/ "sfe.initiated_request.to_auction.errors_count",
        /*description*/
        "Initiated requests by auction that resulted in failure partitioned by "
        "status code",
        /*partition_type*/ "status_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInitiatedRequestBiddingErrorCountByStatus(
        /*name*/ "bfe.initiated_request.to_bidding.errors_count",
        /*description*/
        "Initiated requests by bidding that resulted in failure partitioned by "
        "status code",
        /*partition_type*/ "status_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInitiatedRequestBfeErrorCountByStatus(
        /*name*/ "sfe.initiated_request.to_bfe.errors_count_by_status",
        /*description*/
        "Initiated requests by BFE that resulted in failure partitioned by "
        "status code",
        /*partition_type*/ "status_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kDeviceSignalsSize("bfe.device_signals.size_bytes",
                       "Cumulative size of device_signals in Bytes",
                       server_common::metrics::kSizeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kAdRenderIDsSize("bfe.ad_render_ids.size_bytes",
                     "Cumulative size of ad_render_ids in Bytes",
                     server_common::metrics::kSizeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kBiddingSignalKeysSize("bfe.bidding_signal_keys.size_bytes",
                           "Cumulative size of bidding_signal_keys in Bytes",
                           server_common::metrics::kSizeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kUserBiddingSignalsSize("bfe.user_bidding_signals.size_bytes",
                            "Cumulative size of user_bidding_signals in Bytes",
                            server_common::metrics::kSizeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kComponentAdsSize("bfe.component_ads.size_bytes",
                      "Cumulative size of component_ads in Bytes",
                      server_common::metrics::kSizeHistogram);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kIGCount("bfe.interest_groups.count", "Total number of interest groups",
             kCountHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kInferenceRequestCount(
        /*name*/ "inference.request.count",
        /*description*/
        "Total number of inference requests received by the inference sidecar",
        1, 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInferenceRequestDuration(
        "inference.request.duration_ms",
        "Time taken by inference sidecar to execute inference",
        server_common::metrics::kTimeHistogram, 300, 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInferenceRequestSize("inference.request.size_bytes",
                          "Size of the inference request coming to sidecar",
                          server_common::metrics::kSizeHistogram, 100'000, 100);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInferenceRequestFailedCountByStatus(
        /*name*/ "inference.request.failed_count_by_status",
        /*description*/
        "Total number of inference requests resulted in failure partitioned by "
        "status code",
        /*partition_type*/ "status_code",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInferenceResponseSize(
        "inference.response.size_bytes",
        "Size of the inference response going out of the sidecar",
        server_common::metrics::kSizeHistogram, 100'000, 100);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInferenceRequestCountByModel(
        /*name*/ "inference.request.count_by_model",
        /*description*/
        "Total number of inference request per model",
        /*partition_type*/ "model",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ kDefaultDynamicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInferenceRequestDurationByModel(
        /*name*/ "inference.request.duration_ms_by_model",
        /*description*/
        "Time taken by inference sidecar to execute inference by model",
        /*partition_type*/ "model",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ kDefaultDynamicPartition,
        /*upper_bound*/ 300,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInferenceRequestFailedCountByModel(
        /*name*/ "inference.request.failed_count_by_model",
        /*description*/
        "Total number of inference requests resulted in failure partitioned by "
        "model",
        /*partition_type*/ "model",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ kDefaultDynamicPartition,
        /*upper_bound*/ 1,
        /*lower_bound*/ 0);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInferenceRequestBatchCountByModel(
        /*name*/ "inference.request.batch_count_by_model",
        /*description*/
        "Total number of inference requests input batch size by model",
        /*partition_type*/ "model",
        /*max_partitions_contributed*/ 1,
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition,
        /*upper_bound*/ 50,
        /*lower_bound*/ 0);

template <typename RequestT>
struct RequestMetric;

template <typename RequestT>
inline auto* MetricContextMap(
    std::unique_ptr<server_common::telemetry::BuildDependentConfig> config =
        nullptr,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metrics::GetContextMap<const RequestT,
                                               RequestMetric<RequestT>::kList>(
      std::move(config), std::move(provider), service, version,
      kServerTotalBudget);
}

// API to get `Context` for bidding server to log metric
inline constexpr const server_common::metrics::DefinitionName*
    kBiddingMetricList[] = {
        &server_common::metrics::kTotalRequestCount,
        &server_common::metrics::kServerTotalTimeMs,
        &server_common::metrics::kRequestByte,
        &server_common::metrics::kResponseByte,
        &kRequestFailedCountByStatus,
        &kBiddingTotalBidsCount,
        &kBiddingZeroBidCount,
        &kBiddingZeroBidPercent,
        &kJSExecutionDuration,
        &kJSExecutionErrorCount,
        &kBiddingErrorCountByErrorCode,
        &kBiddingInferenceRequestDuration,
        &kInferenceCloudFetchSuccessCount,
        &kInferenceCloudFetchFailedCountByStatus,
        &kInferenceRecentModelRegistrationSuccess,
        &kInferenceModelRegistrationFailedCountByStatus,
        &kInferenceRecentModelRegistrationFailure,
        &kInferenceAvailableModels,
        &kInferenceRequestCount,
        &kInferenceRequestDuration,
        &kInferenceRequestSize,
        &kInferenceResponseSize,
        &kInferenceRequestFailedCountByStatus,
        &kInferenceErrorCountByErrorCode,
        &kInferenceRequestCountByModel,
        &kInferenceRequestDurationByModel,
        &kInferenceRequestFailedCountByModel,
        &kPASGenerateBidJSExecutionDuration,
        &kPASPrepareDataForRetrievalJSExecutionDuration,
        &kInferenceRequestBatchCountByModel,
};

template <>
struct RequestMetric<google::protobuf::Message> {
  static constexpr absl::Span<
      const server_common::metrics::DefinitionName* const>
      kList = kBiddingMetricList;
  using ContextType = server_common::metrics::ServerContext<kList>;
};
inline auto* BiddingContextMap() {
  return MetricContextMap<google::protobuf::Message>();
}
using BiddingContext = RequestMetric<google::protobuf::Message>::ContextType;

// API to get `Context` for BFE server to log metric
inline constexpr const server_common::metrics::DefinitionName*
    kBfeMetricList[] = {
        &server_common::metrics::kTotalRequestCount,
        &server_common::metrics::kServerTotalTimeMs,
        &server_common::metrics::kRequestByte,
        &server_common::metrics::kResponseByte,
        &kInitiatedRequestKVErrorCountByStatus,
        &kInitiatedRequestBiddingErrorCountByStatus,
        &kRequestFailedCountByStatus,
        &kInitiatedRequestKVDuration,
        &kInitiatedRequestCountByServer,
        &kInitiatedRequestBiddingDuration,
        &kInitiatedRequestKVSize,
        &kInitiatedRequestBiddingSize,
        &kBfeInitiatedResponseKVSize,
        &kInitiatedResponseBiddingSize,
        &kBfeErrorCountByErrorCode,
        &kDeviceSignalsSize,
        &kAdRenderIDsSize,
        &kBiddingSignalKeysSize,
        &kUserBiddingSignalsSize,
        &kComponentAdsSize,
        &kIGCount,
};

template <>
struct RequestMetric<GetBidsRequest> {
  static constexpr absl::Span<
      const server_common::metrics::DefinitionName* const>
      kList = kBfeMetricList;
  using ContextType = server_common::metrics::ServerContext<kList>;
};
inline auto* BfeContextMap() { return MetricContextMap<GetBidsRequest>(); }
using BfeContext = RequestMetric<GetBidsRequest>::ContextType;

// API to get `Context` for SFE server to log metric
inline constexpr const server_common::metrics::DefinitionName*
    kSfeMetricList[] = {
        &server_common::metrics::kTotalRequestCount,
        &server_common::metrics::kServerTotalTimeMs,
        &server_common::metrics::kRequestByte,
        &server_common::metrics::kResponseByte,
        &kInitiatedRequestKVErrorCountByStatus,
        &kInitiatedRequestAuctionErrorCountByStatus,
        &kInitiatedRequestBfeErrorCountByStatus,
        &kRequestFailedCountByStatus,
        &kSfeInitiatedRequestErrorsCountByBuyer,
        &kSfeInitiatedRequestDurationByBuyer,
        &kSfeInitiatedRequestCountByBuyer,
        &kSfeInitiatedResponseSizeByBuyer,
        &kSfeInitiatedRequestSizeByBuyer,
        &kInitiatedRequestKVDuration,
        &kInitiatedRequestCountByServer,
        &kInitiatedRequestAuctionDuration,
        &kInitiatedRequestKVSize,
        &kInitiatedRequestAuctionSize,
        &kSfeInitiatedResponseKVSize,
        &kInitiatedResponseAuctionSize,
        &kSfeErrorCountByErrorCode,
        &kRequestWithWinnerCount,
        &kSfeWithWinnerTimeMs,
        &kProtectedCiphertextSize,
        &kAuctionConfigSize,
        &kAuctionBidRejectedCount,
};

template <>
struct RequestMetric<SelectAdRequest> {
  static constexpr absl::Span<
      const server_common::metrics::DefinitionName* const>
      kList = kSfeMetricList;
  using ContextType = server_common::metrics::ServerContext<kList>;
};
inline auto* SfeContextMap() { return MetricContextMap<SelectAdRequest>(); }
using SfeContext = RequestMetric<SelectAdRequest>::ContextType;

// API to get `Context` for Auction server to log metric
inline constexpr const server_common::metrics::DefinitionName*
    kAuctionMetricList[] = {
        &server_common::metrics::kTotalRequestCount,
        &server_common::metrics::kServerTotalTimeMs,
        &server_common::metrics::kRequestByte,
        &server_common::metrics::kResponseByte,
        &kRequestFailedCountByStatus,
        &kAuctionTotalBidsCount,
        &kAuctionBidRejectedCount,
        &kAuctionBidRejectedPercent,
        &kJSExecutionDuration,
        &kJSExecutionErrorCount,
        &kAuctionErrorCountByErrorCode,
};

template <>
struct RequestMetric<ScoreAdsRequest> {
  static constexpr absl::Span<
      const server_common::metrics::DefinitionName* const>
      kList = kAuctionMetricList;
  using ContextType = server_common::metrics::ServerContext<kList>;
};
inline auto* AuctionContextMap() { return MetricContextMap<ScoreAdsRequest>(); }
using AuctionContext = RequestMetric<ScoreAdsRequest>::ContextType;

}  // namespace metric

inline void LogIfError(
    const absl::Status& s, absl::string_view message = "when logging metric",
    server_common::SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG) {
  if (s.ok()) return;
  ABSL_LOG_EVERY_N_SEC(WARNING, 60)
          .AtLocation(location.file_name(), location.line())
      << message << ": " << s;
}

// ToDo(b/298399657): Move utility function, class to another file
namespace metric {

// InitiatedRequest contains info about a remote request sent to another server,
// logs metrics at destruction
template <typename ContextT>
class InitiatedRequest {
 public:
  static std::unique_ptr<InitiatedRequest<ContextT>> Get(
      absl::string_view request_destination, ContextT* context) {
    return absl::WrapUnique(new InitiatedRequest(request_destination, context));
  }
  void SetRequestSize(int request_size) { request_size_ = request_size; }

  void SetResponseSize(int response_size) { response_size_ = response_size; }

  void SetBuyer(std::string buyer) { buyer_ = std::move(buyer); }

  ~InitiatedRequest() { LogMetrics(); }

 private:
  InitiatedRequest(absl::string_view request_destination, ContextT* context)
      : destination_(request_destination),
        start_(absl::Now()),
        metric_context_(*context) {}

  void LogMetrics() {
    int initiated_request_ms = (absl::Now() - start_) / absl::Milliseconds(1);
    LogIfError(
        metric_context_
            .template AccumulateMetric<metric::kInitiatedRequestCountByServer>(
                1, destination_));

    if (destination_ == metric::kBfe) {
      if constexpr (std::is_same_v<ContextT, SfeContext>) {
        LogPerBuyer(initiated_request_ms);
      }
    }

    if (destination_ == metric::kKv) {
      if constexpr (std::is_same_v<ContextT, BfeContext>) {
        LogOneServer<metric::kInitiatedRequestKVDuration,
                     metric::kInitiatedRequestKVSize,
                     metric::kBfeInitiatedResponseKVSize>(initiated_request_ms);
      }
      if constexpr (std::is_same_v<ContextT, SfeContext>) {
        LogOneServer<metric::kInitiatedRequestKVDuration,
                     metric::kInitiatedRequestKVSize,
                     metric::kSfeInitiatedResponseKVSize>(initiated_request_ms);
      }
    } else if (destination_ == metric::kBs) {
      if constexpr (std::is_same_v<ContextT, BfeContext>) {
        LogOneServer<metric::kInitiatedRequestBiddingDuration,
                     metric::kInitiatedRequestBiddingSize,
                     metric::kInitiatedResponseBiddingSize>(
            initiated_request_ms);
      }
    } else if (destination_ == metric::kAs) {
      if constexpr (std::is_same_v<ContextT, SfeContext>) {
        LogOneServer<metric::kInitiatedRequestAuctionDuration,
                     metric::kInitiatedRequestAuctionSize,
                     metric::kInitiatedResponseAuctionSize>(
            initiated_request_ms);
      }
    }
  }

  template <const auto& DurationMetric, const auto& RequestSizeMetric,
            const auto& ResponseSizeMetric>
  void LogOneServer(int initiated_request_ms) {
    LogIfError(metric_context_.template LogHistogram<DurationMetric>(
        initiated_request_ms));
    LogIfError(metric_context_.template LogHistogram<RequestSizeMetric>(
        request_size_));
    LogIfError(metric_context_.template LogHistogram<ResponseSizeMetric>(
        response_size_));
  }

  void LogPerBuyer(int initiated_request_ms) {
    LogIfError(metric_context_.template AccumulateMetric<
               metric::kSfeInitiatedRequestCountByBuyer>(1, buyer_));
    LogIfError(metric_context_.template AccumulateMetric<
               metric::kSfeInitiatedRequestDurationByBuyer>(
        initiated_request_ms, buyer_));
    LogIfError(metric_context_
                   .template AccumulateMetric<kSfeInitiatedRequestSizeByBuyer>(
                       request_size_, buyer_));
    LogIfError(metric_context_
                   .template AccumulateMetric<kSfeInitiatedResponseSizeByBuyer>(
                       response_size_, buyer_));
  }

  std::string destination_;
  absl::Time start_;
  ContextT& metric_context_;
  int request_size_ = 0;
  int response_size_ = 0;
  std::string buyer_ = "";
};

template <typename ContextT>
std::unique_ptr<InitiatedRequest<ContextT>> MakeInitiatedRequest(
    absl::string_view request_destination, ContextT* context) {
  return InitiatedRequest<ContextT>::Get(request_destination, context);
}

}  // namespace metric

template <typename T>
inline void AddSystemMetric(T* context_map) {
  context_map->AddObserverable(server_common::metrics::kCpuPercent,
                               server_common::GetCpu);
  context_map->AddObserverable(server_common::metrics::kMemoryKB,
                               server_common::GetMemory);
  context_map->AddObserverable(metric::kThreadCount, server_common::GetThread);
  context_map->AddObserverable(
      server_common::metrics::kKeyFetchFailureCount,
      server_common::KeyFetchResultCounter::GetKeyFetchFailureCount);
  context_map->AddObserverable(
      server_common::metrics::kNumKeysParsed,
      server_common::KeyFetchResultCounter::GetNumKeysParsed);
  context_map->AddObserverable(
      server_common::metrics::kNumKeysCached,
      server_common::KeyFetchResultCounter::GetNumKeysCached);
}

inline void AddBuyerPartition(
    server_common::telemetry::BuildDependentConfig& telemetry_config,
    const std::vector<std::string>& buyer_list) {
  std::vector<std::string_view> buyer_list_view = {buyer_list.begin(),
                                                   buyer_list.end()};
  telemetry_config.SetPartition(
      metric::kSfeInitiatedRequestErrorsCountByBuyer.name_, buyer_list_view);
  telemetry_config.SetPartition(metric::kSfeInitiatedRequestCountByBuyer.name_,
                                buyer_list_view);
  telemetry_config.SetPartition(
      metric::kSfeInitiatedRequestDurationByBuyer.name_, buyer_list_view);
  telemetry_config.SetPartition(metric::kSfeInitiatedRequestSizeByBuyer.name_,
                                buyer_list_view);
  telemetry_config.SetPartition(metric::kSfeInitiatedResponseSizeByBuyer.name_,
                                buyer_list_view);
}

inline void AddModelPartition(
    server_common::telemetry::BuildDependentConfig& telemetry_config,
    const std::vector<std::string>& model_list) {
  std::vector<std::string_view> model_list_view = {model_list.begin(),
                                                   model_list.end()};
  telemetry_config.SetPartition(metric::kInferenceRequestCountByModel.name_,
                                model_list_view);
  telemetry_config.SetPartition(metric::kInferenceRequestDurationByModel.name_,
                                model_list_view);
  telemetry_config.SetPartition(
      metric::kInferenceRequestFailedCountByModel.name_, model_list_view);
  telemetry_config.SetPartition(
      metric::kInferenceRequestBatchCountByModel.name_, model_list_view);
}

inline std::vector<std::string> GetErrorList() {
  std::vector<std::string> error_list;

  absl::StatusCode begin = absl::StatusCode::kOk;
  absl::StatusCode end = absl::StatusCode::kUnauthenticated;
  for (absl::StatusCode code = begin; code <= end;
       code = static_cast<absl::StatusCode>(static_cast<int>(code) + 1)) {
    error_list.push_back(StatusCodeToString(code));
  }
  return error_list;
}

inline void AddErrorTypePartition(
    server_common::telemetry::BuildDependentConfig& telemetry_config,
    std::string_view server_name) {
  std::vector<std::string> error_list = GetErrorList();
  std::vector<std::string_view> error_list_view = {error_list.begin(),
                                                   error_list.end()};
  if (server_name == metric::kBfe) {
    telemetry_config.SetPartition(
        metric::kInitiatedRequestKVErrorCountByStatus.name_, error_list_view);
    telemetry_config.SetPartition(
        metric::kInitiatedRequestBiddingErrorCountByStatus.name_,
        error_list_view);
  }
  if (server_name == metric::kSfe) {
    telemetry_config.SetPartition(
        metric::kInitiatedRequestKVErrorCountByStatus.name_, error_list_view);
    telemetry_config.SetPartition(
        metric::kInitiatedRequestBfeErrorCountByStatus.name_, error_list_view);
    telemetry_config.SetPartition(
        metric::kInitiatedRequestAuctionErrorCountByStatus.name_,
        error_list_view);
  }
  if (server_name == metric::kBs) {
    telemetry_config.SetPartition(
        metric::kInferenceRequestFailedCountByStatus.name_, error_list_view);
  }
}

template <typename RequestT, typename ResponseT>
void LogCommonMetric(const RequestT* request, const ResponseT* response) {
  auto& metric_context = metric::MetricContextMap<RequestT>()->Get(request);
  LogIfError(metric_context.template LogUpDownCounterDeferred<
             server_common::metrics::kTotalRequestCount>(
      []() -> int { return 1; }));
  LogIfError(metric_context.template LogHistogramDeferred<
             server_common::metrics::kServerTotalTimeMs>(
      [start = absl::Now()]() -> int {
        return (absl::Now() - start) / absl::Milliseconds(1);
      }));
  LogIfError(
      metric_context
          .template LogHistogramDeferred<server_common::metrics::kRequestByte>(
              [request]() -> int { return request->ByteSizeLong(); }));
  LogIfError(
      metric_context
          .template LogHistogramDeferred<server_common::metrics::kResponseByte>(
              [response]() -> int { return response->ByteSizeLong(); }));
  LogIfError(metric_context.template LogUpDownCounterDeferred<
             metric::kRequestFailedCountByStatus>(
      [&metric_context]() -> absl::flat_hash_map<std::string, int> {
        const absl::Status& result = metric_context.request_result();
        if (result.ok()) {
          return {};
        }
        std::string failure_string = result.ToString();
        if (!absl::StrContains(failure_string, kFailCurl)) {
          return {{std::move(failure_string), 1}};
        }
        // split at "?" to remove url parameter
        std::vector<absl::string_view> substr =
            absl::StrSplit(failure_string, '?');
        return {{std::string(substr[0]), 1}};
      }));
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_METRIC_SERVER_DEFINITION_H_
