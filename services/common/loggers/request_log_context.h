/*
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

#ifndef SERVICES_COMMON_LOGGERS_REQUEST_LOG_CONTEXT_H_
#define SERVICES_COMMON_LOGGERS_REQUEST_LOG_CONTEXT_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/discrete_distribution.h"
#include "absl/random/random.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "src/logger/request_context_impl.h"

#define EVENT_MESSAGE_PROVIDER_SET(T, field)                 \
  void Set(T _##field) {                                     \
    *event_message_.mutable_##field() = std::move(_##field); \
  }

#define EVENT_MESSAGE_PROVIDER_SET_RESPONSE(T, field)        \
  void Set(T _##field) {                                     \
    _##field.clear_debug_info();                             \
    *event_message_.mutable_##field() = std::move(_##field); \
  }

namespace privacy_sandbox::bidding_auction_servers {

inline server_common::log::SystemLogContext& SystemLogContext() {
  return server_common::log::SystemLogContext::Get();
}

class EventMessageProvider {
 public:
  const EventMessage& Get() {
    if (!event_message_.meta_data().is_consented()) {
      if (event_message_.has_protected_auction()) {
        event_message_.mutable_protected_auction()->clear_buyer_input();
      }
      if (event_message_.has_protected_audience()) {
        event_message_.mutable_protected_audience()->clear_buyer_input();
      }
    }
    return event_message_;
  }

  EVENT_MESSAGE_PROVIDER_SET(SelectAdRequest, select_ad_request);
  EVENT_MESSAGE_PROVIDER_SET(ProtectedAuctionInput, protected_auction);
  EVENT_MESSAGE_PROVIDER_SET(ProtectedAudienceInput, protected_audience);
  EVENT_MESSAGE_PROVIDER_SET(AuctionResult, auction_result);

  void Set(const std::vector<AuctionResult>& component_auction_result) {
    event_message_.mutable_component_auction_result()->Assign(
        component_auction_result.begin(), component_auction_result.end());
  }

  EVENT_MESSAGE_PROVIDER_SET(GetBidsRequest, get_bid_request);
  EVENT_MESSAGE_PROVIDER_SET(GetBidsRequest::GetBidsRawRequest,
                             get_bid_raw_request);
  EVENT_MESSAGE_PROVIDER_SET_RESPONSE(GetBidsResponse::GetBidsRawResponse,
                                      get_bid_raw_response);

  EVENT_MESSAGE_PROVIDER_SET(GenerateBidsRequest, generate_bid_request);
  EVENT_MESSAGE_PROVIDER_SET(GenerateBidsRequest::GenerateBidsRawRequest,
                             generate_bid_raw_request);
  EVENT_MESSAGE_PROVIDER_SET_RESPONSE(
      GenerateBidsResponse::GenerateBidsRawResponse, generate_bid_raw_response);

  EVENT_MESSAGE_PROVIDER_SET(GenerateProtectedAppSignalsBidsRequest,
                             generate_app_signal_request);
  EVENT_MESSAGE_PROVIDER_SET(GenerateProtectedAppSignalsBidsRequest::
                                 GenerateProtectedAppSignalsBidsRawRequest,
                             generate_app_signal_raw_request);
  EVENT_MESSAGE_PROVIDER_SET_RESPONSE(
      GenerateProtectedAppSignalsBidsResponse ::
          GenerateProtectedAppSignalsBidsRawResponse,
      generate_app_signal_raw_response);

  EVENT_MESSAGE_PROVIDER_SET(ScoreAdsRequest, score_ad_request);
  EVENT_MESSAGE_PROVIDER_SET(ScoreAdsRequest::ScoreAdsRawRequest,
                             score_ad_raw_request);
  EVENT_MESSAGE_PROVIDER_SET_RESPONSE(ScoreAdsResponse::ScoreAdsRawResponse,
                                      score_ad_raw_response);

  EVENT_MESSAGE_PROVIDER_SET(EventMessage::MetaData, meta_data);

  void Set(absl::string_view udf_log) { event_message_.add_udf_log(udf_log); }

  EVENT_MESSAGE_PROVIDER_SET(EventMessage::KvSignal, kv_signal);

  bool ShouldExport() const { return !event_message_.udf_log().empty(); }

 private:
  EventMessage event_message_;
};

using RequestLogContext = server_common::log::ContextImpl<EventMessageProvider>;

struct RequestContext {
  RequestLogContext& log;
};

inline RequestContext NoOpContext() {
  static absl::NoDestructor<RequestLogContext> log_context(
      RequestLogContext{{}, server_common::ConsentedDebugConfiguration()});
  return {*log_context};
}

// use single ']' as separator
constexpr absl::string_view kFailCurl = "Failed to curl]";

// log verbosity

inline constexpr int kPlain = 1;  // plaintext B&A request and response served
inline constexpr int kNoisyWarn =
    2;  // non-critical error, use PS_LOG(ERROR, *) for critical error
inline constexpr int kUdfLog = 3;
inline constexpr int kSuccess = 3;
inline constexpr int kNoisyInfo = 5;
inline constexpr int kDispatch = 5;  // UDF dispatch request and response
inline constexpr int kOriginated =
    6;  // plaintext B&A request and response originated from server
inline constexpr int kKVLog = 4;  // KV request response
inline constexpr int kStats = 5;  // Stats log e.g. time, byte size, etc.
inline constexpr int kEncrypted = 6;

inline bool AllowAnyEventLogging(RequestLogContext& log_context) {
  // if is_debug_response in non prod, it logs to debug kNoisyInfo
  // if is_consented, it logs to event message
  return (log_context.is_debug_response() && !server_common::log::IsProd()) ||
         log_context.is_consented() || log_context.is_prod_debug();
}

inline bool AllowAnyUdfLogging(RequestLogContext& log_context) {
  return server_common::log::PS_VLOG_IS_ON(kUdfLog) ||
         AllowAnyEventLogging(log_context);
}

inline EventMessage::KvSignal KvEventMessage(
    absl::string_view request, RequestLogContext& log_context,
    absl::Span<const std::string> headers = {}) {
  EventMessage::KvSignal signal;
  if (!AllowAnyEventLogging(log_context)) {
    return signal;
  }
  signal.set_request(request);
  for (absl::string_view header : headers) {
    signal.add_request_header(header);
  }
  return signal;
}

inline void SetKvEventMessage(absl::string_view kv_client,
                              absl::string_view response,
                              EventMessage::KvSignal kv_signal,
                              RequestLogContext& log_context) {
  if (!server_common::log::PS_VLOG_IS_ON(kKVLog)) {
    return;
  }
  PS_VLOG(kKVLog, log_context)
      << kv_client << " response exported in EventMessage if consented";
  if (AllowAnyEventLogging(log_context)) {
    kv_signal.set_response(response);
    log_context.SetEventMessageField(std::move(kv_signal));
  }
}

inline bool RandomSample(int sample_rate_micro, absl::BitGenRef bitgen) {
  absl::discrete_distribution dist(
      {1e6 - sample_rate_micro, (double)sample_rate_micro});
  return dist(bitgen);
}

inline bool SetGeneratorAndSample(int debug_sample_rate_micro,
                                  bool chaffing_enabled, bool request_eligible,
                                  absl::string_view gen_id,
                                  std::optional<std::mt19937>& generator) {
  bool is_debug_eligible = request_eligible && debug_sample_rate_micro > 0;
  if (chaffing_enabled || is_debug_eligible) {
    generator = std::mt19937(std::hash<std::string>{}(gen_id.data()));
  } else {
    generator = std::nullopt;
  }
  return is_debug_eligible && RandomSample(debug_sample_rate_micro, *generator);
}

// in non_prod, modify config to consent
void ModifyConsent(server_common::ConsentedDebugConfiguration& original);

inline constexpr absl::string_view kProdDebug = "sampled_debug";

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_LOGGERS_REQUEST_LOG_CONTEXT_H_
