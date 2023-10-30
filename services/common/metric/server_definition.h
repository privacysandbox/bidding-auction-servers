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

#include "services/common/metric/error_code.h"
#include "services/common/util/read_system.h"
#include "services/common/util/reporting_util.h"
#include "src/cpp/metric/context_map.h"

// Defines API used by Bidding auction servers, and B&A specific metrics.
namespace privacy_sandbox::bidding_auction_servers {
namespace metric {

inline constexpr server_common::metrics::PrivacyBudget server_total_budget{
    /*epsilon*/ 5};

inline constexpr double kPercentHistogram[] = {0.06, 0.12, 0.25, 0.5, 1};

inline constexpr absl::string_view kAs = "AS";
inline constexpr absl::string_view kBs = "BS";
inline constexpr absl::string_view kKv = "KV";
inline constexpr absl::string_view kBfe = "BFE";
inline constexpr absl::string_view kSfe = "SFE";

inline constexpr absl::string_view kServerName[]{
    kAs, kBfe, kBs, kKv, kSfe,
};

// Metric Definitions that are specific to bidding & auction servers.
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kJSExecutionDuration("js_execution.duration_ms",
                         "Time taken to execute the JS dispatcher",
                         server_common::metrics::kTimeHistogram);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kJSExecutionErrorCount("js_execution.error.count",
                           "No. of times js execution returned status != OK");

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestKVDuration(
        "initiated_request.kv.duration_ms",
        "Total duration request takes to get response back from KV server",
        server_common::metrics::kTimeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestBiddingDuration(
        "initiated_request.bidding.duration_ms",
        "Total duration request takes to get response back from bidding server",
        server_common::metrics::kTimeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestAuctionDuration(
        "initiated_request.auction.duration_ms",
        "Total duration request takes to get response back from Auction server",
        server_common::metrics::kTimeHistogram);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInitiatedRequestErrorCountByServer(
        /*name*/ "initiated_request.errors_count_by_server",
        /*description*/
        "Total number of errors occurred for the requests initiated by the "
        "server partitioned by outgoing server",
        /*partition_type*/ "server name",
        /* public_partitions*/ kServerName);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestKVSize(
        "initiated_request.kv.size_bytes",
        "Size of the Initiated Request to KV server in Bytes",
        server_common::metrics::kSizeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestBiddingSize(
        "initiated_request.bidding.size_bytes",
        "Size of the Initiated Request to Bidding server in Bytes",
        server_common::metrics::kSizeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedRequestAuctionSize(
        "initiated_request.auction.size_bytes",
        "Size of the initiated Request to Auction server in Bytes",
        server_common::metrics::kSizeHistogram);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedResponseKVSize(
        "initiated_response.kv.size_bytes",
        "Size of the Initiated Response by KV server in Bytes",
        server_common::metrics::kSizeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedResponseBiddingSize(
        "initiated_response.bidding.size_bytes",
        "Size of the Initiated Response by Bidding server in Bytes",
        server_common::metrics::kSizeHistogram);
inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kInitiatedResponseAuctionSize(
        "initiated_response.auction.size_bytes",
        "Size of the initiated Response by Auction server in Bytes",
        server_common::metrics::kSizeHistogram);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInitiatedRequestCountByServer(
        /*name*/ "initiated_request.count_by_server",
        /*description*/
        "Total number of requests initiated by the server partitioned by "
        "outgoing server",
        /*partition_type*/ "server name",
        /* public_partitions*/ kServerName);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kBiddingZeroBidCount(
        "business_logic.bidding.zero_bid.count",
        "Total number of times bidding service returns a zero bid");

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kBiddingZeroBidPercent(
        "business_logic.bidding.zero_bid.percent",
        "Percentage of times bidding service returns a zero bid",
        kPercentHistogram);

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

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kAuctionBidRejectedCount(
        /*name*/ "business_logic.auction.bid_rejected.count",
        /*description*/
        "Total number of times auction service rejects a bid partitioned by the"
        "seller rejection reason",
        /*partition_type*/ "seller_rejection_reason",
        /*public_partitions*/ kSellerRejectReasons);

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kHistogram>
    kAuctionBidRejectedPercent(
        /*name*/ "business_logic.auction.bid_rejected.percent",
        /*description*/
        "Percentage of times auction service rejects a bid", kPercentHistogram);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kBiddingTotalBidsCount(
        /*name*/ "business_logic.bidding.bids.count",
        /*description*/ "Total number of bids generated by bidding service");

inline constexpr server_common::metrics::Definition<
    double, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kGauge>
    kThreadCount("system.thread.count", "Thread Count");

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kUpDownCounter>
    kAuctionTotalBidsCount(
        /*name*/ "business_logic.auction.bids.count",
        /*description*/
        "Total number of bids used to score in auction service");

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kAuctionErrorCountByErrorCode(
        /*name*/ "auction.error_code",
        /*description*/
        "Number of errors in the auction server by error code",
        /*partition_type*/ "error code",
        /*public_partitions*/ kAuctionErrorCode);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kBfeErrorCountByErrorCode(
        /*name*/ "bfe.error_code",
        /*description*/
        "Number of errors in the BFE server by error code",
        /*partition_type*/ "error code",
        /*public_partitions*/ kBfeErrorCode);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kBiddingErrorCountByErrorCode(
        /*name*/ "bidding.error_code",
        /*description*/
        "Number of errors in the bidding server by error code",
        /*partition_type*/ "error code",
        /*public_partitions*/ kBiddingErrorCode);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeErrorCountByErrorCode(
        /*name*/ "sfe.error_code",
        /*description*/
        "Number of errors in the SFE server by error code",
        /*partition_type*/ "error code",
        /*public_partitions*/ kSfeErrorCode);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedRequestErrorsCountByBuyer(
        /*name*/ "sfe.initiated_request.errors_count_by_buyer",
        /*description*/
        "Total number of initiated requests failed per buyer",
        /*partition_type*/ "buyer",
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedRequestCountByBuyer(
        /*name*/ "sfe.initiated_request.count_by_buyer",
        /*description*/
        "Total number of initiated requests per buyer",
        /*partition_type*/ "buyer",
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedRequestDurationByBuyer(
        /*name*/ "sfe.initiated_request.duration_by_buyer",
        /*description*/
        "Initiated requests duration  per buyer",
        /*partition_type*/ "buyer",
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedRequestSizeByBuyer(
        /*name*/ "sfe.initiated_request.size_by_buyer",
        /*description*/
        "Initiated requests size  per buyer",
        /*partition_type*/ "buyer",
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kSfeInitiatedResponseSizeByBuyer(
        /*name*/ "sfe.initiated_response.size_by_buyer",
        /*description*/
        "Initiated response size  per buyer",
        /*partition_type*/ "buyer",
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kRequestFailedCountByStatus(
        /*name*/ "request.failed_count_by_status",
        /*description*/
        "Total number of requests that resulted in failure partitioned by "
        "Error Code",
        /*partition_type*/ "error_status_code",
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition);

inline constexpr server_common::metrics::Definition<
    int, server_common::metrics::Privacy::kNonImpacting,
    server_common::metrics::Instrument::kPartitionedCounter>
    kInitiatedRequestErrorCountByStatus(
        /*name*/ "initiated_request.errors_count_by_status",
        /*description*/
        "Initiated requests that resulted in failure partitioned by "
        "Error Code",
        /*partition_type*/ "error_status_code",
        /*public_partitions*/ server_common::metrics::kEmptyPublicPartition);

// API to get `Context` for bidding server to log metric
inline constexpr const server_common::metrics::DefinitionName*
    kBiddingMetricList[] = {
        &server_common::metrics::kTotalRequestCount,
        &server_common::metrics::kTotalRequestFailedCount,
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
};
inline constexpr absl::Span<const server_common::metrics::DefinitionName* const>
    kBiddingMetricSpan = kBiddingMetricList;
inline auto* BiddingContextMap(
    std::optional<server_common::telemetry::BuildDependentConfig> config =
        std::nullopt,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metrics::GetContextMap<const GenerateBidsRequest,
                                               kBiddingMetricSpan>(
      std::move(config), std::move(provider), service, version,
      server_total_budget);
}
using BiddingContext =
    server_common::metrics::ServerContext<kBiddingMetricSpan>;

// API to get `Context` for BFE server to log metric
inline constexpr const server_common::metrics::DefinitionName*
    kBfeMetricList[] = {
        &server_common::metrics::kTotalRequestCount,
        &server_common::metrics::kTotalRequestFailedCount,
        &server_common::metrics::kServerTotalTimeMs,
        &server_common::metrics::kRequestByte,
        &server_common::metrics::kResponseByte,
        &server_common::metrics::kInitiatedRequestCount,
        &server_common::metrics::kInitiatedRequestErrorCount,
        &server_common::metrics::kInitiatedRequestTotalDuration,
        &server_common::metrics::kInitiatedRequestByte,
        &server_common::metrics::kInitiatedResponseByte,
        &kInitiatedRequestErrorCountByStatus,
        &kRequestFailedCountByStatus,
        &kInitiatedRequestErrorCountByServer,
        &kInitiatedRequestKVDuration,
        &kInitiatedRequestCountByServer,
        &kInitiatedRequestBiddingDuration,
        &kInitiatedRequestKVSize,
        &kInitiatedRequestBiddingSize,
        &kInitiatedResponseKVSize,
        &kInitiatedResponseBiddingSize,
        &kBfeErrorCountByErrorCode,
};
inline constexpr absl::Span<const server_common::metrics::DefinitionName* const>
    kBfeMetricSpan = kBfeMetricList;
inline auto* BfeContextMap(
    std::optional<server_common::telemetry::BuildDependentConfig> config =
        std::nullopt,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metrics::GetContextMap<const GetBidsRequest,
                                               kBfeMetricSpan>(
      std::move(config), std::move(provider), service, version,
      server_total_budget);
}
using BfeContext = server_common::metrics::ServerContext<kBfeMetricSpan>;

// API to get `Context` for SFE server to log metric
inline constexpr const server_common::metrics::DefinitionName*
    kSfeMetricList[] = {
        &server_common::metrics::kTotalRequestCount,
        &server_common::metrics::kTotalRequestFailedCount,
        &server_common::metrics::kServerTotalTimeMs,
        &server_common::metrics::kRequestByte,
        &server_common::metrics::kResponseByte,
        &server_common::metrics::kInitiatedRequestCount,
        &server_common::metrics::kInitiatedRequestErrorCount,
        &server_common::metrics::kInitiatedRequestTotalDuration,
        &server_common::metrics::kInitiatedRequestByte,
        &server_common::metrics::kInitiatedResponseByte,
        &kInitiatedRequestErrorCountByStatus,
        &kRequestFailedCountByStatus,
        &kSfeInitiatedRequestErrorsCountByBuyer,
        &kSfeInitiatedRequestDurationByBuyer,
        &kSfeInitiatedRequestCountByBuyer,
        &kSfeInitiatedResponseSizeByBuyer,
        &kSfeInitiatedRequestSizeByBuyer,
        &kInitiatedRequestErrorCountByServer,
        &kInitiatedRequestKVDuration,
        &kInitiatedRequestCountByServer,
        &kInitiatedRequestAuctionDuration,
        &kInitiatedRequestKVSize,
        &kInitiatedRequestAuctionSize,
        &kInitiatedResponseKVSize,
        &kInitiatedResponseAuctionSize,
        &kSfeErrorCountByErrorCode,
};
inline constexpr absl::Span<const server_common::metrics::DefinitionName* const>
    kSfeMetricSpan = kSfeMetricList;
inline auto* SfeContextMap(
    std::optional<server_common::telemetry::BuildDependentConfig> config =
        std::nullopt,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metrics::GetContextMap<const SelectAdRequest,
                                               kSfeMetricSpan>(
      std::move(config), std::move(provider), service, version,
      server_total_budget);
}
using SfeContext = server_common::metrics::ServerContext<kSfeMetricSpan>;

// API to get `Context` for Auction server to log metric
inline constexpr const server_common::metrics::DefinitionName*
    kAuctionMetricList[] = {
        &server_common::metrics::kTotalRequestCount,
        &server_common::metrics::kTotalRequestFailedCount,
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
inline constexpr absl::Span<const server_common::metrics::DefinitionName* const>
    kAuctionMetricSpan = kAuctionMetricList;
inline auto* AuctionContextMap(
    std::optional<server_common::telemetry::BuildDependentConfig> config =
        std::nullopt,
    std::unique_ptr<opentelemetry::metrics::MeterProvider> provider = nullptr,
    absl::string_view service = "", absl::string_view version = "") {
  return server_common::metrics::GetContextMap<const ScoreAdsRequest,
                                               kAuctionMetricSpan>(
      std::move(config), std::move(provider), service, version,
      server_total_budget);
}
using AuctionContext =
    server_common::metrics::ServerContext<kAuctionMetricSpan>;

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

  void SetBuyer(std::string buyer) { buyer_ = buyer; }

  ~InitiatedRequest() { LogMetrics(); }

 private:
  InitiatedRequest(absl::string_view request_destination, ContextT* context)
      : destination_(request_destination),
        start_(absl::Now()),
        metric_context_(*context) {}

  void LogMetrics() {
    int initiated_request_ms = (absl::Now() - start_) / absl::Milliseconds(1);
    LogIfError(metric_context_.template AccumulateMetric<
               server_common::metrics::kInitiatedRequestCount>(1));
    LogIfError(
        metric_context_
            .template AccumulateMetric<metric::kInitiatedRequestCountByServer>(
                1, destination_));
    LogIfError(metric_context_.template AccumulateMetric<
               server_common::metrics::kInitiatedRequestTotalDuration>(
        initiated_request_ms));
    LogIfError(metric_context_.template AccumulateMetric<
               server_common::metrics::kInitiatedRequestByte>(request_size_));
    LogIfError(metric_context_.template AccumulateMetric<
               server_common::metrics::kInitiatedResponseByte>(response_size_));

    if (destination_ == metric::kBfe) {
      if constexpr (std::is_same_v<ContextT, SfeContext>) {
        LogPerBuyer(initiated_request_ms);
      }
    }
    if (destination_ == metric::kKv) {
      if constexpr (std::is_same_v<ContextT, SfeContext> ||
                    std::is_same_v<ContextT, BfeContext>) {
        LogOneServer<metric::kInitiatedRequestKVDuration,
                     metric::kInitiatedRequestKVSize,
                     metric::kInitiatedResponseKVSize>(initiated_request_ms);
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
  int request_size_;
  int response_size_;
  std::string buyer_;
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
  telemetry_config.SetPartition(metric::kRequestFailedCountByStatus.name_,
                                error_list_view);
  if (server_name == metric::kBfe || server_name == metric::kSfe) {
    telemetry_config.SetPartition(
        metric::kInitiatedRequestErrorCountByStatus.name_, error_list_view);
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_METRIC_SERVER_DEFINITION_H_
