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

// Defines API used by Bidding auction servers, and B&A specific metrics.
namespace privacy_sandbox::bidding_auction_servers {
namespace metric {

inline constexpr server_common::metric::PrivacyBudget server_total_budget{
    /*epsilon*/ 5};

// Metric Definitions that are specific to bidding & auction servers.
inline constexpr double kV8TimeHistogram[] = {50,    100,   250,  500,
                                              1'000, 2'000, 4'000};
inline constexpr server_common::metric::Definition<
    int, server_common::metric::Privacy::kNonImpacting,
    server_common::metric::Instrument::kHistogram>
    kJSExecutionDuration("js_execution.duration_ms",
                         "Time taken to execute the JS dispatcher",
                         kV8TimeHistogram);

// API to get `Context` for bidding server to log metric
inline constexpr const server_common::metric::DefinitionName*
    kBiddingMetricList[] = {&server_common::metric::kTotalRequestCount,
                            &server_common::metric::kTotalRequestFailedCount,
                            &server_common::metric::kServerTotalTimeMs,
                            &server_common::metric::kRequestByte,
                            &server_common::metric::kResponseByte,
                            &kJSExecutionDuration};
inline constexpr absl::Span<const server_common::metric::DefinitionName* const>
    kBiddingMetricSpan = kBiddingMetricList;
inline auto* BiddingContextMap(
    std::optional<server_common::BuildDependentConfig> config = std::nullopt,
    server_common::metric::MetricRouter::Meter* meter = nullptr) {
  return server_common::metric::GetContextMap<const GenerateBidsRequest,
                                              kBiddingMetricSpan>(
      std::move(config), meter, server_total_budget);
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
};
inline constexpr absl::Span<const server_common::metric::DefinitionName* const>
    kBfeMetricSpan = kBfeMetricList;
inline auto* BfeContextMap(
    std::optional<server_common::BuildDependentConfig> config = std::nullopt,
    server_common::metric::MetricRouter::Meter* meter = nullptr) {
  return server_common::metric::GetContextMap<const GetBidsRequest,
                                              kBfeMetricSpan>(
      std::move(config), meter, server_total_budget);
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
};
inline constexpr absl::Span<const server_common::metric::DefinitionName* const>
    kSfeMetricSpan = kSfeMetricList;
inline auto* SfeContextMap(
    std::optional<server_common::BuildDependentConfig> config = std::nullopt,
    server_common::metric::MetricRouter::Meter* meter = nullptr) {
  return server_common::metric::GetContextMap<const SelectAdRequest,
                                              kSfeMetricSpan>(
      std::move(config), meter, server_total_budget);
}
using SfeContext = server_common::metric::ServerContext<kSfeMetricSpan>;

// API to get `Context` for Auction server to log metric
inline constexpr const server_common::metric::DefinitionName*
    kAuctionMetricList[] = {&server_common::metric::kTotalRequestCount,
                            &server_common::metric::kTotalRequestFailedCount,
                            &server_common::metric::kServerTotalTimeMs,
                            &server_common::metric::kRequestByte,
                            &server_common::metric::kResponseByte,
                            &kJSExecutionDuration};
inline constexpr absl::Span<const server_common::metric::DefinitionName* const>
    kAuctionMetricSpan = kAuctionMetricList;
inline auto* AuctionContextMap(
    std::optional<server_common::BuildDependentConfig> config = std::nullopt,
    server_common::metric::MetricRouter::Meter* meter = nullptr) {
  return server_common::metric::GetContextMap<const ScoreAdsRequest,
                                              kAuctionMetricSpan>(
      std::move(config), meter, server_total_budget);
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
