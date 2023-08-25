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
#include <utility>

#include "services/common/metric/context_map.h"
#include "services/common/util/read_system.h"
#include "services/common/util/reporting_util.h"

// Defines API used by Bidding auction servers, and B&A specific metrics.
namespace privacy_sandbox::bidding_auction_servers {
namespace metric {

inline constexpr server_common::metric::PrivacyBudget server_total_budget{
    /*epsilon*/ 5};

inline constexpr absl::string_view kAs = "AS";
inline constexpr absl::string_view kBs = "BS";
inline constexpr absl::string_view kKv = "KV";
inline constexpr absl::string_view kBfe = "BFE";

inline constexpr absl::string_view kServerName[]{
    kAs,
    kBfe,
    kBs,
    kKv,
};

// Metric Definitions that are specific to bidding & auction servers.
inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kHistogram>
    kJSExecutionDuration("js_execution.duration_ms",
                         "Time taken to execute the JS dispatcher",
                         server_common::metric::kTimeHistogram);

inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kUpDownCounter>
    kJSExecutionErrorCount("js_execution.error.count",
                           "No. of times js execution returned status != OK");

inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kHistogram>
    kInitiatedRequestKVDuration(
        "initiated_request.kv.duration_ms",
        "Total duration request takes to get response back from KV server",
        server_common::metric::kTimeHistogram);
inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kHistogram>
    kInitiatedRequestBiddingDuration(
        "initiated_request.bidding.duration_ms",
        "Total duration request takes to get response back from bidding server",
        server_common::metric::kTimeHistogram);
inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kHistogram>
    kInitiatedRequestAuctionDuration(
        "initiated_request.auction.duration_ms",
        "Total duration request takes to get response back from Auction server",
        server_common::metric::kTimeHistogram);

inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kHistogram>
    kInitiatedRequestKVSize(
        "initiated_request.kv.size_bytes",
        "Size of the Initiated Request to KV server in Bytes",
        server_common::metric::kSizeHistogram);
inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kHistogram>
    kInitiatedRequestBiddingSize(
        "initiated_request.bidding.size_bytes",
        "Size of the Initiated Request to Bidding server in Bytes",
        server_common::metric::kSizeHistogram);
inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kHistogram>
    kInitiatedRequestAuctionSize(
        "initiated_request.auction.size_bytes",
        "Size of the initiated Request to Auction server in Bytes",
        server_common::metric::kSizeHistogram);

inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kPartitionedCounter>
    kInitiatedRequestCountByServer(
        /*name*/ "initiated_request.count_by_server",
        /*description*/
        "Total number of requests initiated by the server partitioned by "
        "outgoing server",
        /*partition_type*/ "server name",
        /* public_partitions*/ kServerName);

inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kUpDownCounter>
    kBiddingZeroBidCount(
        "business_logic.bidding.zero_bid.count",
        "Total number of times bidding service returns a zero bid");

inline constexpr absl::string_view kSellerRejectReasons[] = {
    kRejectionReasonBidBelowAuctionFloor,
    kRejectionReasonBlockedByPublisher,
    kRejectionReasonCategoryExclusions,
    kRejectionReasonDisapprovedByExchange,
    kRejectionReasonInvalidBid,
    kRejectionReasonLanguageExclusions,
    kRejectionReasonNotAvailable,
    kRejectionReasonPendingApprovalByExchange,
};

inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kPartitionedCounter>
    kAuctionBidRejectedCount(
        /*name*/ "business_logic.auction.bid_rejected.count",
        /*description*/
        "Total number of times auction service rejects a bid partitioned by the"
        "seller rejection reason",
        /*partition_type*/ "seller_rejection_reason",
        /*public_partitions*/ kSellerRejectReasons);

inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kUpDownCounter>
    kBiddingTotalBidsCount(
        /*name*/ "business_logic.bidding.bids.count",
        /*description*/ "Total number of bids generated by bidding service");

inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kUpDownCounter>
    kAuctionTotalBidsCount(
        /*name*/ "business_logic.auction.bids.count",
        /*description*/
        "Total number of bids used to score in auction service");

// API to get `Context` for bidding server to log metric
inline constexpr const server_common::metric::DefinitionName*
    kBiddingMetricList[] = {
        &server_common::metric::kTotalRequestCount,
        &server_common::metric::kTotalRequestFailedCount,
        &server_common::metric::kServerTotalTimeMs,
        &server_common::metric::kRequestByte,
        &server_common::metric::kResponseByte,
        &kBiddingTotalBidsCount,
        &kBiddingZeroBidCount,
        &kJSExecutionDuration,
        &kJSExecutionErrorCount,
};
inline constexpr absl::Span<const server_common::metric::DefinitionName* const>
    kBiddingMetricSpan = kBiddingMetricList;
inline auto* BiddingContextMap(
    std::optional<server_common::BuildDependentConfig> config = std::nullopt,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metric::GetContextMap<const GenerateBidsRequest,
                                              kBiddingMetricSpan>(
      std::move(config), std::move(provider), service, version,
      server_total_budget);
}
using BiddingContext = server_common::metric::ServerContext<kBiddingMetricSpan>;

// API to get `Context` for BFE server to log metric
inline constexpr const server_common::metric::DefinitionName* kBfeMetricList[] =
    {
        &server_common::metric::kTotalRequestCount,
        &server_common::metric::kTotalRequestFailedCount,
        &server_common::metric::kServerTotalTimeMs,
        &server_common::metric::kRequestByte,
        &server_common::metric::kResponseByte,
        &server_common::metric::kInitiatedRequestCount,
        &server_common::metric::kInitiatedRequestErrorCount,
        &server_common::metric::kInitiatedRequestTotalDuration,
        &server_common::metric::kInitiatedRequestByte,
        &kInitiatedRequestKVDuration,
        &kInitiatedRequestCountByServer,
        &kInitiatedRequestBiddingDuration,
        &kInitiatedRequestKVSize,
        &kInitiatedRequestBiddingSize,
};
inline constexpr absl::Span<const server_common::metric::DefinitionName* const>
    kBfeMetricSpan = kBfeMetricList;
inline auto* BfeContextMap(
    std::optional<server_common::BuildDependentConfig> config = std::nullopt,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metric::GetContextMap<const GetBidsRequest,
                                              kBfeMetricSpan>(
      std::move(config), std::move(provider), service, version,
      server_total_budget);
}
using BfeContext = server_common::metric::ServerContext<kBfeMetricSpan>;

// API to get `Context` for SFE server to log metric
inline constexpr const server_common::metric::DefinitionName* kSfeMetricList[] =
    {
        &server_common::metric::kTotalRequestCount,
        &server_common::metric::kTotalRequestFailedCount,
        &server_common::metric::kServerTotalTimeMs,
        &server_common::metric::kRequestByte,
        &server_common::metric::kResponseByte,
        &server_common::metric::kInitiatedRequestCount,
        &server_common::metric::kInitiatedRequestErrorCount,
        &server_common::metric::kInitiatedRequestTotalDuration,
        &server_common::metric::kInitiatedRequestByte,
        &kInitiatedRequestKVDuration,
        &kInitiatedRequestCountByServer,
        &kInitiatedRequestAuctionDuration,
        &kInitiatedRequestKVSize,
        &kInitiatedRequestAuctionSize,
};
inline constexpr absl::Span<const server_common::metric::DefinitionName* const>
    kSfeMetricSpan = kSfeMetricList;
inline auto* SfeContextMap(
    std::optional<server_common::BuildDependentConfig> config = std::nullopt,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metric::GetContextMap<const SelectAdRequest,
                                              kSfeMetricSpan>(
      std::move(config), std::move(provider), service, version,
      server_total_budget);
}
using SfeContext = server_common::metric::ServerContext<kSfeMetricSpan>;

// API to get `Context` for Auction server to log metric
inline constexpr const server_common::metric::DefinitionName*
    kAuctionMetricList[] = {
        &server_common::metric::kTotalRequestCount,
        &server_common::metric::kTotalRequestFailedCount,
        &server_common::metric::kServerTotalTimeMs,
        &server_common::metric::kRequestByte,
        &server_common::metric::kResponseByte,
        &kAuctionTotalBidsCount,
        &kAuctionBidRejectedCount,
        &kJSExecutionDuration,
        &kJSExecutionErrorCount,
};
inline constexpr absl::Span<const server_common::metric::DefinitionName* const>
    kAuctionMetricSpan = kAuctionMetricList;
inline auto* AuctionContextMap(
    std::optional<server_common::BuildDependentConfig> config = std::nullopt,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metric::GetContextMap<const ScoreAdsRequest,
                                              kAuctionMetricSpan>(
      std::move(config), std::move(provider), service, version,
      server_total_budget);
}
using AuctionContext = server_common::metric::ServerContext<kAuctionMetricSpan>;

}  // namespace metric

inline void LogIfError(const absl::Status& s,
                       absl::string_view message = "when logging metric",
                       SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG) {
  if (s.ok()) return;
  ABSL_LOG_EVERY_N_SEC(WARNING, 60)
          .AtLocation(location.file_name(), location.line())
      << message << ": " << s;
}

template <typename T>
inline void AddSystemMetric(T* context_map) {
  context_map->AddObserverable(server_common::metric::kCpuPercent,
                               server_common::GetCpu);
  context_map->AddObserverable(server_common::metric::kMemoryKB,
                               server_common::GetMemory);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_METRIC_SERVER_DEFINITION_H_
