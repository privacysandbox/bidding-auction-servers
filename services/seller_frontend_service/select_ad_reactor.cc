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

#include "services/seller_frontend_service/select_ad_reactor.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/numeric/bits.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "api/k_anon_query.grpc.pb.h"
#include "api/k_anon_query.pb.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/clients/kv_server/kv_v2.h"
#include "services/common/compression/gzip.h"
#include "services/common/constants/user_error_strings.h"
#include "services/common/feature_flags.h"
#include "services/common/reporters/async_reporter.h"
#include "services/common/util/auction_scope_util.h"
#include "services/common/util/hash_util.h"
#include "services/common/util/priority_vector/priority_vector_utils.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/k_anon/constants.h"
#include "services/seller_frontend_service/k_anon/k_anon_utils.h"
#include "services/seller_frontend_service/kv_seller_signals_adapter.h"
#include "services/seller_frontend_service/private_aggregation/private_aggregation_helper.h"
#include "services/seller_frontend_service/util/buyer_input_proto_utils.h"
#include "services/seller_frontend_service/util/chaffing_utils.h"
#include "services/seller_frontend_service/util/key_fetcher_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/communication/ohttp_utils.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr int kMillisInMinute = 60000;
inline constexpr int kSecsInMinute = 60;
inline constexpr int kNumAllowedChromeGhostWinners = 1;
using ::google::protobuf::RepeatedPtrField;
using ScoreAdsRawRequest = ScoreAdsRequest::ScoreAdsRawRequest;
using AdScore = ScoreAdsResponse::AdScore;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using DecodedBuyerInputs =
    absl::flat_hash_map<absl::string_view, BuyerInputForBidding>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using KVLookUpResult =
    absl::StatusOr<std::unique_ptr<kv_server::v2::GetValuesResponse>>;
using ::google::chrome::kanonymityquery::v1::ValidateHashesRequest;
using ::google::chrome::kanonymityquery::v1::ValidateHashesResponse;
using GhostWinnerForTopLevelAuction =
    AuctionResult::KAnonGhostWinner::GhostWinnerForTopLevelAuction;

inline void RecordInterestGroupUpdates(
    std::string buyer, UpdateGroupMap& all_updates,
    GetBidsResponse::GetBidsRawResponse& response) {
  if (!response.update_interest_group_list().interest_groups().empty()) {
    all_updates.emplace(
        std::move(buyer),
        std::move(*response.mutable_update_interest_group_list()));
  }
}

// Calculates the difference between the request_timestamp from the ciphertext
// and the server's clock time.
unsigned int CalculateTimeDifferenceToNowSeconds(int64_t request_timestamp_ms) {
  absl::Duration diff =
      absl::FromUnixMillis(request_timestamp_ms) - absl::Now();
  int64_t diff_seconds = std::abs(absl::ToInt64Seconds(diff));
  if (diff_seconds > std::numeric_limits<int>::max()) {
    return std::numeric_limits<int>::max();
  }

  return static_cast<int>(diff_seconds);
}

}  // namespace

SelectAdReactor::SelectAdReactor(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, server_common::Executor* executor,
    const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client,
    const ReportWinMap& report_win_map,
    const RandomNumberGeneratorFactory& rng_factory, bool enable_cancellation,
    bool enable_kanon, bool enable_buyer_private_aggregate_reporting,
    int per_adtech_paapi_contributions_limit, bool fail_fast,
    int max_buyers_solicited)
    : request_context_(context),
      request_(request),
      response_(response),
      executor_(executor),
      clients_(clients),
      config_client_(config_client),
      report_win_map_(report_win_map),
      rng_factory_(rng_factory),
      auction_scope_(GetAuctionScope(*request_)),
      log_context_({}, server_common::ConsentedDebugConfiguration(),
                   [this]() { return response_->mutable_debug_info(); }),
      error_accumulator_(&log_context_),
      fail_fast_(fail_fast),
      is_protected_auction_request_(false),
      // PAS should only be enabled for single seller auctions.
      is_pas_enabled_(
          config_client_.GetBooleanParameter(ENABLE_PROTECTED_APP_SIGNALS) &&
          (auction_scope_ == AuctionScope::AUCTION_SCOPE_SINGLE_SELLER)),
      is_protected_audience_enabled_(
          config_client_.GetBooleanParameter(ENABLE_PROTECTED_AUDIENCE)),
      is_tkv_v2_browser_enabled_(
          config_client_.GetBooleanParameter(ENABLE_TKV_V2_BROWSER)),
      chaffing_enabled_(config_client_.GetBooleanParameter(ENABLE_CHAFFING)),
      chaffing_v2_enabled_(
          config_client_.GetBooleanParameter(ENABLE_CHAFFING_V2)),
      buyer_caching_enabled_(
          config_client_.GetBooleanParameter(ENABLE_BUYER_CACHING)),
      max_buyers_solicited_(chaffing_enabled_
                                ? kMaxBuyersSolicitedChaffingEnabled
                                : max_buyers_solicited),
      enable_cancellation_(enable_cancellation),
      enable_enforce_kanon_(enable_kanon),
      enable_buyer_private_aggregate_reporting_(
          enable_buyer_private_aggregate_reporting),
      per_adtech_paapi_contributions_limit_(
          per_adtech_paapi_contributions_limit),
      priority_signals_vector_(rapidjson::kObjectType),
      async_task_tracker_(
          0, log_context_,  // NumTasksToTrack is set appropriately in Execute()
          [this](bool successful) { OnAllBidsDone(successful); }),
      k_anon_api_key_(config_client_.GetStringParameter(K_ANON_API_KEY)),
      perform_scoring_signals_fetch_(
          config_client.GetStringParameter(SCORING_SIGNALS_FETCH_MODE) !=
          kSignalsNotFetched),
      fetch_scoring_signals_query_kanon_tracker_(
          1, log_context_, [this](bool unused) {
            PS_VLOG(5) << (perform_scoring_signals_fetch_
                               ? "Scoring signals fetched "
                               : "")
                       << (perform_scoring_signals_fetch_ &&
                                   enable_enforce_kanon_
                               ? " and "
                               : "")
                       << (enable_enforce_kanon_ ? " k-anon query done " : " ");
            OnFetchScoringSignalsDone(std::move(maybe_scoring_signals_));
          }) {
  if (config_client_.GetBooleanParameter(ENABLE_SELLER_FRONTEND_BENCHMARKING)) {
    benchmarking_logger_ =
        std::make_unique<BuildInputProcessResponseBenchmarkingLogger>(
            FormatTime(absl::Now()));
  } else {
    benchmarking_logger_ = std::make_unique<NoOpsLogger>();
  }

  PS_CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::SfeContextMap()->Remove(request_));
    return absl::OkStatus();
  }()) << "SfeContextMap()->Get(request) should have been called";
}

grpc::Status SelectAdReactor::ExtractAuctionConfig() {
  int num_auction_configs_in_req = request_->has_auction_config() +
                                   request_->has_compressed_auction_config();
  if (num_auction_configs_in_req != 1) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "Provide one AuctionConfig value in SelectAdRequest");
  }

  if (request_->has_auction_config()) {
    auction_config_ = request_->auction_config();
    return grpc::Status(grpc::OK, "");
  }

  // Client provided the compressed_auction_config field.
  bool allow_compressed_auction_config =
      config_client_.HasParameter(ALLOW_COMPRESSED_AUCTION_CONFIG) &&
      config_client_.GetBooleanParameter(ALLOW_COMPRESSED_AUCTION_CONFIG);
  if (!allow_compressed_auction_config) {
    return grpc::Status(
        grpc::INVALID_ARGUMENT,
        "Server is not accepting compressed AuctionConfig protos; "
        "provide the uncompressed_auction_config field in SelectAdRequest");
  }

  // Only gzip is currently supported; assume gzip if compressed_auction_config
  // is provided.
  absl::StatusOr<std::string> decompressed =
      GzipDecompress(request_->compressed_auction_config().auction_config());
  if (!decompressed.ok()) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "Unable to decompress compressed_auction_config field");
  }

  if (!auction_config_.ParseFromString(*decompressed)) {
    return grpc::Status(grpc::INVALID_ARGUMENT,
                        "Unable to parse AuctionConfig proto from "
                        "decompressed compressed_auction_config");
  }

  return grpc::Status(grpc::OK, "");
}

AdWithBidMetadata SelectAdReactor::BuildAdWithBidMetadata(
    const AdWithBid& input, absl::string_view interest_group_owner,
    bool k_anon_status) {
  AdWithBidMetadata result;
  // First copy all the fields that can be copied directly.
  if (input.has_ad()) {
    *result.mutable_ad() = input.ad();
  }
  result.set_bid(input.bid());
  result.set_render(input.render());
  result.set_allow_component_auction(input.allow_component_auction());
  result.mutable_ad_components()->CopyFrom(input.ad_components());
  result.set_interest_group_name(input.interest_group_name());
  result.set_ad_cost(input.ad_cost());
  result.set_modeling_signals(input.modeling_signals());
  result.set_bid_currency(input.bid_currency());
  result.set_data_version(input.data_version());

  // Then set fields passed in externally.
  result.set_interest_group_owner(interest_group_owner);

  // Finally, find the AdWithBid's IG and copy the last fields from there.
  const BuyerInputForBidding& buyer_input_for_bidding =
      buyer_inputs_->find(interest_group_owner)->second;
  int interest_group_idx = 0;
  for (const auto& interest_group : buyer_input_for_bidding.interest_groups()) {
    if (interest_group.name() == result.interest_group_name()) {
      result.set_interest_group_origin(interest_group.origin());
      if (request_->client_type() == CLIENT_TYPE_BROWSER) {
        result.set_join_count(interest_group.browser_signals().join_count());
        PS_VLOG(kNoisyInfo, log_context_)
            << "BrowserSignal: Recency:"
            << interest_group.browser_signals().recency();
        if (interest_group.browser_signals().has_recency_ms()) {
          // ScoreAdsRequest requires recency to be in minutes.
          result.set_recency(static_cast<int>(
              interest_group.browser_signals().recency_ms() / kMillisInMinute));
        } else {
          // ScoreAdsRequest requires recency to be in minutes.
          result.set_recency(static_cast<int>(
              interest_group.browser_signals().recency() / kSecsInMinute));
        }
      }
      InterestGroupIdentity ig = {
          .interest_group_owner = interest_group_owner.data(),
          .interest_group_name = result.interest_group_name()};
      interest_group_index_map_.try_emplace(std::move(ig), interest_group_idx);
      result.set_interest_group_idx(interest_group_idx);
      // May as well skip further iterations.
      break;
    }
    interest_group_idx++;
  }
  if (enable_enforce_kanon_) {
    result.set_k_anon_status(k_anon_status);
  } else {
    result.set_k_anon_status(true);
  }
  if (!input.buyer_reporting_id().empty()) {
    result.set_buyer_reporting_id(input.buyer_reporting_id());
  }
  if (!input.buyer_and_seller_reporting_id().empty()) {
    result.set_buyer_and_seller_reporting_id(
        input.buyer_and_seller_reporting_id());
  }
  if (!input.selected_buyer_and_seller_reporting_id().empty()) {
    result.set_selected_buyer_and_seller_reporting_id(
        input.selected_buyer_and_seller_reporting_id());
  }
  return result;
}

bool SelectAdReactor::HaveClientVisibleErrors() {
  return !error_accumulator_.GetErrors(ErrorVisibility::CLIENT_VISIBLE).empty();
}

bool SelectAdReactor::HaveAdServerVisibleErrors() {
  return !error_accumulator_.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE)
              .empty();
}

void SelectAdReactor::MayPopulateClientVisibleErrors() {
  if (!HaveClientVisibleErrors()) {
    return;
  }

  error_.set_code(static_cast<int>(ErrorCode::CLIENT_SIDE));
  error_.set_message(error_accumulator_.GetAccumulatedErrorString(
      ErrorVisibility::CLIENT_VISIBLE));
}

grpc::Status SelectAdReactor::DecryptRequest() {
  if (request_->protected_auction_ciphertext().empty() &&
      request_->protected_audience_ciphertext().empty()) {
    return {grpc::StatusCode::INVALID_ARGUMENT,
            kEmptyProtectedAuctionCiphertextError};
  }
  is_protected_auction_request_ =
      !request_->protected_auction_ciphertext().empty();

  absl::string_view encapsulated_req;
  if (is_protected_auction_request_) {
    encapsulated_req = request_->protected_auction_ciphertext();
  } else {
    encapsulated_req = request_->protected_audience_ciphertext();
  }

  PS_VLOG(5) << "Protected "
             << (is_protected_auction_request_ ? "auction" : "audience")
             << " ciphertext: " << absl::Base64Escape(encapsulated_req);

  auto decrypted_hpke_req = DecryptOHTTPEncapsulatedHpkeCiphertext(
      encapsulated_req, clients_.key_fetcher_manager_);
  if (!decrypted_hpke_req.ok()) {
    PS_VLOG(kNoisyWarn, SystemLogContext())
        << "Error decrypting the protected "
        << (is_protected_auction_request_ ? "auction" : "audience")
        << " input ciphertext" << decrypted_hpke_req.status();
    return {grpc::StatusCode::INVALID_ARGUMENT,
            absl::StrFormat(kErrorDecryptingCiphertextError,
                            decrypted_hpke_req.status().message())};
  }

  PS_VLOG(5) << "Successfully decrypted the protected "
             << (is_protected_auction_request_ ? "auction" : "audience")
             << " input ciphertext";

  decrypted_request_ = std::move(*decrypted_hpke_req);
  if (is_protected_auction_request_) {
    protected_auction_input_ =
        GetDecodedProtectedAuctionInput(decrypted_request_->plaintext);
  } else {
    protected_auction_input_ =
        GetDecodedProtectedAudienceInput(decrypted_request_->plaintext);
  }
  std::visit(
      [this](auto& input) {
        buyer_inputs_ = GetDecodedBuyerinputs(input.buyer_input());
        enable_enforce_kanon_ &= input.enforce_kanon();
        if (enable_enforce_kanon_) {
          fetch_scoring_signals_query_kanon_tracker_.SetNumTasksToTrack(
              (perform_scoring_signals_fetch_ ? 1 : 0) + 1);
        }
      },
      protected_auction_input_);

  return grpc::Status::OK;
}

void SelectAdReactor::MayPopulateAdServerVisibleErrors() {
  if (auction_config_.seller_signals().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptySellerSignals,
                ErrorCode::CLIENT_SIDE);
  }

  if (auction_config_.auction_signals().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptyAuctionSignals,
                ErrorCode::CLIENT_SIDE);
  }

  if (auction_config_.buyer_list().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptyBuyerList,
                ErrorCode::CLIENT_SIDE);
  }

  if (auction_config_.seller().empty()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptySeller,
                ErrorCode::CLIENT_SIDE);
  }

  if (!auction_config_.seller_currency().empty()) {
    if (!std::regex_match(auction_config_.seller_currency(),
                          GetValidCurrencyCodeRegex())) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kInvalidSellerCurrency,
                  ErrorCode::CLIENT_SIDE);
    }
  }

  if (config_client_.GetStringParameter(SELLER_ORIGIN_DOMAIN) !=
      auction_config_.seller()) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kWrongSellerDomain,
                ErrorCode::CLIENT_SIDE);
  }

  for (const auto& [buyer, per_buyer_config] :
       auction_config_.per_buyer_config()) {
    if (buyer.empty()) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                  kEmptyBuyerInPerBuyerConfig, ErrorCode::CLIENT_SIDE);
    }
    if (per_buyer_config.buyer_signals().empty()) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                  absl::StrFormat(kEmptyBuyerSignals, buyer),
                  ErrorCode::CLIENT_SIDE);
    }
    if (!per_buyer_config.buyer_currency().empty()) {
      if (!std::regex_match(per_buyer_config.buyer_currency(),
                            GetValidCurrencyCodeRegex())) {
        ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kInvalidBuyerCurrency,
                    ErrorCode::CLIENT_SIDE);
      }
    }
  }

  // Device Component Auction not allowed with Android client type.
  if (request_->client_type() == CLIENT_TYPE_ANDROID &&
      auction_scope_ ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                kDeviceComponentAuctionWithAndroid, ErrorCode::CLIENT_SIDE);
  }

  if (config_client_.HasParameter(ENABLE_PRIORITY_VECTOR) &&
      config_client_.GetBooleanParameter(ENABLE_PRIORITY_VECTOR) &&
      !auction_config_.priority_signals().empty()) {
    absl::StatusOr<rapidjson::Document> priority_signals_vector =
        ParsePriorityVector(auction_config_.priority_signals());
    if (!priority_signals_vector.ok()) {
      ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                  "Malformed priority signals JSON", ErrorCode::CLIENT_SIDE);
    }

    priority_signals_vector_ = *std::move(priority_signals_vector);
  }
}

void SelectAdReactor::MayLogBuyerInput() {
  if (!buyer_inputs_.ok()) {
    PS_LOG(ERROR, log_context_) << "Failed to decode buyer inputs";
  } else {
    PS_VLOG(6, log_context_)
        << "Decoded BuyerInput:\n"
        << absl::StrJoin(
               *buyer_inputs_, "\n",
               absl::PairFormatter(absl::AlphaNumFormatter(), " : ",
                                   [](std::string* out, const auto& bi) {
                                     absl::StrAppend(
                                         out, "{", bi.ShortDebugString(), "}");
                                   }));
  }
}

ChaffingConfig SelectAdReactor::GetChaffingConfig(
    const absl::flat_hash_set<absl::string_view>& auction_config_buyer_set) {
  // The server has served this request before; use cached values.
  if (buyer_caching_enabled_ && previously_invoked_buyers_) {
    return {.chaff_request_candidates =
                previously_invoked_buyers_->chaff_buyer_names,
            .num_chaff_requests =
                (int)previously_invoked_buyers_->chaff_buyer_names.size(),
            .num_real_requests =
                (int)previously_invoked_buyers_->non_chaff_buyer_names.size()};
  }

  int num_chaff_requests = 0;
  std::vector<absl::string_view> chaff_request_candidates;

  // 'real' requests means non-chaff requests, e.g. buyers that will actually
  // generate bids to show an ad to this client.
  int num_real_requests = 0;

  // Loop through the buyers this SFE instance is configured to talk to.
  for (const auto& [buyer, unused] : clients_.buyer_factory.Entries()) {
    (void)unused;
    // Check if the buyer is contained in the top level auction config of the
    // SelectAd request.
    if (!auction_config_buyer_set.contains(buyer)) {
      continue;
    }

    // Next, check if the buyer is present in the buyer_input field of the
    // request (i.e. the ciphertext).
    if (buyer_inputs_->find(buyer) == buyer_inputs_->end()) {
      // If the buyer is NOT in the ciphertext, e.g. the buyer's interest
      // groups, they are a chaff request candidate.
      chaff_request_candidates.push_back(buyer);
    } else {
      // If the buyer was present in all 3 (SFE config, auction config, and
      // ciphertext), we will send this buyer a 'real' GetBids request.
      num_real_requests++;
    }
  }

  if (!chaffing_enabled_ ||
      ShouldSkipChaffing(clients_.buyer_factory.Entries().size(), *rng_)) {
    return {.num_real_requests = num_real_requests};
  }

  // Use the RNG seeded w/ the generation ID to deterministically determine
  // how many chaff requests to send for a given ciphertext.
  if (!chaff_request_candidates.empty()) {
    // We cannot have a static lower bound for the number of chaff requests the
    // SFE sends. If we did and a network snooper ever saw that many requests go
    // out, that would leak that the client was not associated with any of the
    // buyers. To avoid this, we (attempt to) set the lower bound for
    // num_chaff_requests to 2 when num_real_requests = 0, and 1 otherwise.
    int num_chaff_requests_lower_bound = kMinChaffRequests;
    if (num_real_requests == 0) {
      num_chaff_requests_lower_bound = kMinChaffRequestsWithNoRealRequests;
    }

    num_chaff_requests_lower_bound = std::min(
        num_chaff_requests_lower_bound, (int)chaff_request_candidates.size());

    if (rng_) {
      num_chaff_requests = rng_->GetUniformInt(num_chaff_requests_lower_bound,
                                               chaff_request_candidates.size());
    }
  }

  if (rng_) {
    rng_->Shuffle(chaff_request_candidates);
  }

  return {.chaff_request_candidates = std::move(chaff_request_candidates),
          .num_chaff_requests = num_chaff_requests,
          .num_real_requests = num_real_requests};
}

int SelectAdReactor::GetEffectiveNumberOfBuyers(
    const ChaffingConfig& chaffing_config) {
  if (chaffing_enabled_) {
    return chaffing_config.num_real_requests +
           chaffing_config.num_chaff_requests;
  }

  return chaffing_config.num_real_requests;
}

std::vector<absl::string_view> SelectAdReactor::GetNonChaffBuyerNames(
    const absl::flat_hash_set<absl::string_view>& auction_config_buyer_set,
    const ChaffingConfig& chaffing_config) {
  std::vector<absl::string_view> non_chaff_buyer_names;

  // Loop through the server's map of configured buyers. This takes care of the
  // case of duplicated buyers in the auction_config.buyer_list.
  // NOLINTNEXTLINE
  for (const auto& [buyer_ig_owner, unused] :
       clients_.buyer_factory.Entries()) {
    if (chaffing_enabled_) {
      auto it = std::find(chaffing_config.chaff_request_candidates.begin(),
                          chaffing_config.chaff_request_candidates.end(),
                          buyer_ig_owner);
      if (it != chaffing_config.chaff_request_candidates.end()) {
        // We verify above that any buyers in chaff_request_candidates are not
        // present in the ciphertext.
        continue;
      }
    }

    if (!auction_config_buyer_set.contains(buyer_ig_owner)) {
      continue;
    }

    const auto& buyer_input_iterator = buyer_inputs_->find(buyer_ig_owner);
    if (buyer_input_iterator == buyer_inputs_->end()) {
      PS_VLOG(kNoisyWarn, log_context_)
          << "No buyer input found for buyer: " << buyer_ig_owner
          << ", skipping buyer";
      continue;
    }

    non_chaff_buyer_names.emplace_back(buyer_ig_owner);
  }

  return non_chaff_buyer_names;
}

void SelectAdReactor::FetchBids() {
  absl::flat_hash_set<absl::string_view> auction_config_buyer_set(
      auction_config_.buyer_list().begin(), auction_config_.buyer_list().end());
  ChaffingConfig chaffing_config = GetChaffingConfig(auction_config_buyer_set);
  int effective_number_of_buyers = GetEffectiveNumberOfBuyers(chaffing_config);
  async_task_tracker_.SetNumTasksToTrack(effective_number_of_buyers);
  if (effective_number_of_buyers == 0) {
    OnAllBidsDone(true);
    return;
  }

  std::vector<absl::string_view> non_chaff_buyer_candidates;
  absl::Span<absl::string_view> chaff_buyer_candidates;
  if (buyer_caching_enabled_ && previously_invoked_buyers_) {
    non_chaff_buyer_candidates =
        previously_invoked_buyers_->non_chaff_buyer_names;
    chaff_buyer_candidates =
        absl::MakeSpan(previously_invoked_buyers_->chaff_buyer_names);
  } else {
    non_chaff_buyer_candidates =
        GetNonChaffBuyerNames(auction_config_buyer_set, chaffing_config);
    chaff_buyer_candidates =
        absl::MakeSpan(chaffing_config.chaff_request_candidates);
  }

  std::vector<std::pair<absl::string_view,
                        std::unique_ptr<GetBidsRequest::GetBidsRawRequest>>>
      buyer_get_bids_requests;
  for (absl::string_view buyer_ig_owner : non_chaff_buyer_candidates) {
    const auto& buyer_input_iterator = buyer_inputs_->find(buyer_ig_owner);
    if (buyer_input_iterator == buyer_inputs_->end()) {
      continue;
    }

    auto get_bids_request = CreateGetBidsRequest(buyer_ig_owner.data(),
                                                 buyer_input_iterator->second);
    buyer_get_bids_requests.push_back(
        {buyer_ig_owner, std::move(get_bids_request)});
  }

  // Loop through chaff candidates and send 'num_chaff_requests' requests.
  for (auto it = chaff_buyer_candidates.begin();
       it != chaff_buyer_candidates.end(); ++it) {
    DCHECK(chaffing_enabled_ || chaffing_v2_enabled_);
    if (std::distance(chaff_buyer_candidates.begin(), it) >=
        chaffing_config.num_chaff_requests) {
      break;
    }

    auto get_bids_request =
        std::make_unique<GetBidsRequest::GetBidsRawRequest>();
    get_bids_request->set_is_chaff(true);
    auto* log_context = get_bids_request->mutable_log_context();
    std::visit(
        [log_context](const auto& protected_auction_input) {
          log_context->set_generation_id(
              protected_auction_input.generation_id());
        },
        protected_auction_input_);
    buyer_get_bids_requests.push_back(
        {it->data(), std::move(get_bids_request)});
  }

  if (chaffing_enabled_ && rng_) {
    rng_->Shuffle(buyer_get_bids_requests);
  }

  int num_buyers_solicited = 0;

  std::vector<absl::string_view> chaff_buyers;
  std::vector<absl::string_view> non_chaff_buyers;
  for (auto& [buyer_name, request] : buyer_get_bids_requests) {
    // Only send the first 'max_buyers_solicited_' requests, whether they're
    // chaff or real.
    if (num_buyers_solicited >= max_buyers_solicited_) {
      // Skipped buyers should not be left pending.
      async_task_tracker_.TaskCompleted(TaskStatus::SKIPPED);
      PS_VLOG(kNoisyWarn, log_context_)
          << "Exceeded cap of " << max_buyers_solicited_
          << " buyers called. Skipping buyer";
      continue;
    }

    if (request->is_chaff()) {
      chaff_buyers.emplace_back(buyer_name);
    } else {
      non_chaff_buyers.emplace_back(buyer_name);
    }

    PS_VLOG(kNoisyInfo, log_context_) << "Invoking buyer: " << buyer_name;
    FetchBid(buyer_name.data(), std::move(request));
    num_buyers_solicited++;
  }

  if (buyer_caching_enabled_ && !previously_invoked_buyers_) {
    std::visit(
        [this, moved_chaff_buyers = std::move(chaff_buyers),
         moved_non_chaff_buyers =
             std::move(non_chaff_buyers)](auto& protected_input) mutable {
          InvokedBuyers invoked_buyers = {
              .chaff_buyer_names = std::move(moved_chaff_buyers),
              .non_chaff_buyer_names = std::move(moved_non_chaff_buyers)};
          absl::Status insert_result = clients_.invoked_buyers_cache->Insert(
              {{protected_input.generation_id(), std::move(invoked_buyers)}});
          if (!insert_result.ok()) {
            PS_VLOG(kPlain, log_context_)
                << "Failed to insert into chaff cache for request with "
                   "generation ID "
                << protected_input.generation_id();
          }
        },
        protected_auction_input_);
  }
}

void SelectAdReactor::Execute() {
  if (enable_cancellation_ && request_context_->IsCancelled()) {
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  // TODO(b/278039901): Add integration test for metadata forwarding.
  absl::StatusOr<RequestMetadata> buyer_metadata =
      GrpcMetadataToRequestMetadata(request_context_->client_metadata(),
                                    kBuyerMetadataKeysMap);
  if (!buyer_metadata.ok()) {
    PS_VLOG(kNoisyWarn) << buyer_metadata.status();
    FinishWithStatus(server_common::FromAbslStatus(buyer_metadata.status()));
    return;
  }

  buyer_metadata_ = *std::move(buyer_metadata);

  grpc::Status extracted_auction_config = ExtractAuctionConfig();
  if (!extracted_auction_config.ok()) {
    FinishWithStatus(extracted_auction_config);
    return;
  }

  async_task_tracker_.SetNumTasksToTrack(auction_config_.buyer_list_size());

  grpc::Status decrypt_status = DecryptRequest();

  std::visit(
      [this](auto& protected_input) mutable {
        bool is_debug_eligible =
            request_->client_type() == CLIENT_TYPE_ANDROID &&
            protected_input.enable_unlimited_egress();
        rng_ =
            CreateRng(config_client_.GetIntParameter(DEBUG_SAMPLE_RATE_MICRO),
                      chaffing_enabled_, is_debug_eligible,
                      protected_input.generation_id(), rng_factory_);
        is_sampled_for_debug_ = ShouldSample(
            config_client_.GetIntParameter(DEBUG_SAMPLE_RATE_MICRO),
            is_debug_eligible, *rng_);

        if (config_client_.GetBooleanParameter(CONSENT_ALL_REQUESTS)) {
          ModifyConsent(*protected_input.mutable_consented_debug_config());
        }
        // Populates the logging context needed for request tracing. should be
        // called after decrypting and decoding the request.
        log_context_.Update(
            {
                {kGenerationId, protected_input.generation_id()},
                {kSellerDebugId, auction_config_.seller_debug_id()},
            },
            protected_input.consented_debug_config(), is_sampled_for_debug_);

        LogIfError(metric_context_->LogHistogram<metric::kRequestAgeSeconds>(
            (int)CalculateTimeDifferenceToNowSeconds(
                protected_input.request_timestamp_ms())));

        if (!buyer_caching_enabled_) {
          return;
        }

        absl::flat_hash_map<std::string, InvokedBuyers> invoked_buyers =
            clients_.invoked_buyers_cache->Query(
                {protected_input.generation_id()});
        if (!invoked_buyers.empty()) {
          previously_invoked_buyers_ =
              std::move(invoked_buyers.begin()->second);
        }
      },
      protected_auction_input_);

  EventMessage::MetaData meta_data;
  meta_data.set_is_consented(log_context_.is_consented());
  meta_data.set_is_prod_debug(log_context_.is_prod_debug());
  log_context_.SetEventMessageField(std::move(meta_data));

  if (log_context_.is_consented()) {
    std::string generation_id = std::visit(
        [](const auto& protected_input) {
          return protected_input.generation_id();
        },
        protected_auction_input_);
    metric_context_->SetConsented(std::move(generation_id));
  } else if (log_context_.is_prod_debug()) {
    metric_context_->SetConsented(kProdDebug.data());
  }

  if (is_protected_auction_request_) {
    LogIfError(metric_context_->LogHistogram<metric::kProtectedCiphertextSize>(
        (int)request_->protected_auction_ciphertext().size()));
  } else {
    LogIfError(metric_context_->LogHistogram<metric::kProtectedCiphertextSize>(
        (int)request_->protected_audience_ciphertext().size()));
  }
  LogIfError(metric_context_->LogHistogram<metric::kAuctionConfigSize>(
      (int)auction_config_.ByteSizeLong()));

  if (server_common::log::PS_VLOG_IS_ON(kEncrypted)) {
    PS_VLOG(kEncrypted, log_context_)
        << "Encrypted SelectAdRequest exported in EventMessage if consented";
    log_context_.SetEventMessageField(*request_);
  }

  PS_VLOG(kPlain, log_context_)
      << "Headers:\n"
      << absl::StrJoin(request_context_->client_metadata(), "\n",
                       absl::PairFormatter(absl::StreamFormatter(), " : ",
                                           absl::StreamFormatter()));
  if (!decrypt_status.ok()) {
    FinishWithStatus(decrypt_status);
    PS_LOG(ERROR, log_context_) << "SelectAdRequest decryption failed:"
                                << server_common::ToAbslStatus(decrypt_status);
    return;
  }
  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    std::visit(
        [this](auto& input) {
          log_context_.SetEventMessageField(input);
          PS_VLOG(kPlain, log_context_)
              << (is_protected_auction_request_ ? "ProtectedAuctionInput"
                                                : "ProtectedAudienceInput")
              << " exported in EventMessage if consented";
        },
        protected_auction_input_);
  }
  MayLogBuyerInput();
  MayPopulateAdServerVisibleErrors();
  if (HaveAdServerVisibleErrors()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeSelectAdRequestBadInput));
    PS_LOG(ERROR, log_context_) << "AdServerVisible errors found, failing now";

    // Finish the GRPC request if we have received bad data from the ad tech
    // server.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }

  // Validate mandatory fields if decoding went through fine.
  if (!HaveClientVisibleErrors()) {
    PS_VLOG(5, log_context_)
        << "No ClientVisible errors found, validating input now";

    std::visit(
        [this](const auto& protected_auction_input) {
          ValidateProtectedAuctionInput(protected_auction_input);
        },
        protected_auction_input_);
  }

  // Populate errors on the response immediately after decoding and input
  // validation so that when we stop processing the request due to errors, we
  // have correct errors set in the response.
  MayPopulateClientVisibleErrors();

  if (error_accumulator_.HasErrors()) {
    PS_LOG(ERROR, log_context_) << "Some errors found, failing now";

    // Finish the GRPC request now.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  PS_VLOG(kNoisyInfo, log_context_) << "No client / Adtech server errors found";

  benchmarking_logger_->Begin();

  PS_VLOG(6, log_context_) << "Buyer list size: "
                           << auction_config_.buyer_list().size();

  FetchBids();
}

std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
SelectAdReactor::CreateGetBidsRequest(
    const std::string& buyer_ig_owner,
    const BuyerInputForBidding& buyer_input_for_bidding) {
  auto get_bids_request = std::make_unique<GetBidsRequest::GetBidsRawRequest>();
  get_bids_request->set_is_chaff(false);
  get_bids_request->set_seller(auction_config_.seller());
  get_bids_request->set_client_type(request_->client_type());
  get_bids_request->set_auction_signals(auction_config_.auction_signals());
  std::string buyer_debug_id;
  const auto& per_buyer_config_itr =
      auction_config_.per_buyer_config().find(buyer_ig_owner);
  if (per_buyer_config_itr != auction_config_.per_buyer_config().end()) {
    buyer_debug_id = per_buyer_config_itr->second.buyer_debug_id();
    if (per_buyer_config_itr->second.has_buyer_kv_experiment_group_id()) {
      get_bids_request->set_buyer_kv_experiment_group_id(
          per_buyer_config_itr->second.buyer_kv_experiment_group_id());
    }
    if (per_buyer_config_itr->second.has_blob_versions()) {
      *get_bids_request->mutable_blob_versions() =
          per_buyer_config_itr->second.blob_versions();
    }
    if (!per_buyer_config_itr->second.buyer_signals().empty()) {
      get_bids_request->set_buyer_signals(
          per_buyer_config_itr->second.buyer_signals());
    }
    if (enable_enforce_kanon_) {
      get_bids_request->set_multi_bid_limit(
          per_buyer_config_itr->second.per_buyer_multi_bid_limit());
    }
  }

  *get_bids_request->mutable_buyer_input() =
      ToBuyerInput(buyer_input_for_bidding);
  *get_bids_request->mutable_buyer_input_for_bidding() =
      buyer_input_for_bidding;

  get_bids_request->set_top_level_seller(auction_config_.top_level_seller());
  std::visit(
      [&get_bids_request, &buyer_debug_id,
       this](const auto& protected_auction_input) {
        get_bids_request->set_publisher_name(
            protected_auction_input.publisher_name());
        get_bids_request->set_enable_debug_reporting(
            protected_auction_input.enable_debug_reporting());
        get_bids_request->set_enable_sampled_debug_reporting(
            protected_auction_input.fdo_flags()
                .enable_sampled_debug_reporting());
        auto* log_context = get_bids_request->mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(buyer_debug_id);
        if (protected_auction_input.has_consented_debug_config()) {
          *get_bids_request->mutable_consented_debug_config() =
              protected_auction_input.consented_debug_config();
        }
        get_bids_request->set_enable_unlimited_egress(
            protected_auction_input.enable_unlimited_egress());
        if (enable_enforce_kanon_) {
          get_bids_request->set_enforce_kanon(
              protected_auction_input.enforce_kanon());
        }
      },
      protected_auction_input_);

  if (!is_protected_audience_enabled_ &&
      get_bids_request->mutable_buyer_input_for_bidding()
              ->interest_groups_size() > 0) {
    PS_VLOG(kNoisyWarn)
        << "Clearing interest groups in the input since protected "
           "audience support is disabled";
    get_bids_request->mutable_buyer_input_for_bidding()
        ->clear_interest_groups();
  }

  if (config_client_.HasParameter(ENABLE_PRIORITY_VECTOR) &&
      config_client_.GetBooleanParameter(ENABLE_PRIORITY_VECTOR)) {
    auto priority_signals = GetBuyerPrioritySignals(
        priority_signals_vector_, auction_config_.per_buyer_config(),
        buyer_ig_owner);
    if (priority_signals.ok()) {
      get_bids_request->set_priority_signals(*std::move(priority_signals));
    }
  }

  return get_bids_request;
}

void SelectAdReactor::FetchBid(
    const std::string& buyer_ig_owner,
    std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request) {
  if (enable_cancellation_ && request_context_->IsCancelled()) {
    async_task_tracker_.TaskCompleted(TaskStatus::CANCELLED);
    return;
  }

  const std::shared_ptr<BuyerFrontEndAsyncClient>& buyer_client =
      clients_.buyer_factory.Get(buyer_ig_owner);

  if (buyer_client == nullptr) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "No buyer client found for buyer: " << buyer_ig_owner;

    async_task_tracker_.TaskCompleted(TaskStatus::SKIPPED);
    return;
  }

  PS_VLOG(6, log_context_) << "Getting bid from a BFE";

  absl::Duration timeout = absl::Milliseconds(
      config_client_.GetIntParameter(GET_BID_RPC_TIMEOUT_MS));
  if (auction_config_.buyer_timeout_ms() > 0) {
    timeout = absl::Milliseconds(auction_config_.buyer_timeout_ms());
  }

  // gets deleted in execute internal callback
  auto bfe_request =
      metric::MakeInitiatedRequest(metric::kBfe, metric_context_.get())
          .release();
  bfe_request->SetBuyer(buyer_ig_owner);

  RequestConfig request_config;
  const bool is_chaff_request = get_bids_request->is_chaff();
  if (chaffing_enabled_ && !chaffing_v2_enabled_ && rng_) {
    request_config = GetChaffingV1GetBidsRequestConfig(is_chaff_request, *rng_);
  } else if (chaffing_enabled_ && chaffing_v2_enabled_) {
    request_config = GetChaffingV2GetBidsRequestConfig(
        buyer_ig_owner, is_chaff_request, *clients_.moving_median_manager,
        {log_context_});
  }

  // If using the old request format, add a gRPC header to signal the payload is
  // compressed.
  RequestMetadata buyer_metadata_copy = buyer_metadata_;
  if (absl::string_view header_flag =
          config_client_.GetStringParameter(HEADER_PASSED_TO_BUYER);
      !header_flag.empty()) {
    std::vector<std::string> headers =
        absl::StrSplit(header_flag, ',', absl::SkipWhitespace());
    for (const std::string& header : headers) {
      if (auto itr = request_context_->client_metadata().find(
              absl::AsciiStrToLower(header));
          itr != request_context_->client_metadata().end()) {
        std::string client_metadata_val =
            std::string(itr->second.begin(), itr->second.end());
        if (!IsValidHeader(header, client_metadata_val)) {
          PS_VLOG(kNoisyWarn, log_context_) << absl::StrFormat(
              kIllegalHeaderError, header, client_metadata_val);
          continue;
        }
        buyer_metadata_copy.insert(
            {absl::AsciiStrToLower(header), std::move(client_metadata_val)});
      }
    }
  }

  grpc::ClientContext* client_context =
      client_contexts_.Add(buyer_metadata_copy);
  absl::Status execute_result = buyer_client->ExecuteInternal(
      std::move(get_bids_request), client_context,
      CancellationWrapper(
          request_context_, enable_cancellation_,
          [buyer_ig_owner, this, bfe_request, buyer_client, is_chaff_request](
              absl::StatusOr<
                  std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
                  response,
              ResponseMetadata response_metadata) mutable {
            {
              bfe_request->SetRequestSize((int)response_metadata.request_size);
              bfe_request->SetResponseSize(
                  (int)response_metadata.response_size);

              if (chaffing_v2_enabled_ && !is_chaff_request) {
                absl::Status add_result =
                    clients_.moving_median_manager->AddNumberToBuyerWindow(
                        buyer_ig_owner, *rng_, response_metadata.request_size);
                if (!add_result.ok()) {
                  PS_LOG(ERROR, log_context_) << add_result;
                }
              }

              // destruct bfe_request, destructor measures request time
              delete bfe_request;
            }
            PS_VLOG(6, log_context_) << "Received a response from a BFE";
            OnFetchBidsDone(std::move(response), buyer_ig_owner);
          },
          [&async_task_tracker_ = async_task_tracker_,
           bfe_request]() {  // OnCancel
            delete bfe_request;
            async_task_tracker_.TaskCompleted(TaskStatus::CANCELLED);
          }),
      timeout, request_config);
  if (!execute_result.ok()) {
    delete bfe_request;
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeGetBidsFailedToCall));
    PS_LOG(ERROR, log_context_) << absl::StrFormat(
        "Failed to make async GetBids call: (buyer: %s, "
        "seller: %s, error: "
        "%s)",
        buyer_ig_owner, auction_config_.seller(), execute_result.ToString());
    async_task_tracker_.TaskCompleted(TaskStatus::ERROR);
  }
}

void SelectAdReactor::LogInitiatedRequestErrorMetrics(
    absl::string_view server_name, const absl::Status& status,
    absl::string_view buyer) {
  if (server_name == metric::kAs) {
    LogIfError(metric_context_->AccumulateMetric<
               metric::kInitiatedRequestAuctionErrorCountByStatus>(
        1, StatusCodeToString(status.code())));
  } else if (server_name == metric::kKv) {
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kInitiatedRequestKVErrorCountByStatus>(
                1, StatusCodeToString(status.code())));
  } else if (server_name == metric::kBfe) {
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kSfeInitiatedRequestErrorsCountByBuyer>(
                1, buyer));
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kInitiatedRequestBfeErrorCountByStatus>(
                1, StatusCodeToString(status.code())));
  } else if (server_name == metric::kKAnon) {
    LogIfError(metric_context_->AccumulateMetric<
               metric::kSfeInitiatedRequestKanonErrorCountByStatus>(
        1, (StatusCodeToString(status.code()))));
  }
}

void SelectAdReactor::OnFetchBidsDone(
    absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
        response,
    const std::string& buyer_ig_owner) {
  PS_VLOG(5, log_context_) << "Received response from a BFE ... ";
  if (response.ok()) {
    auto& found_response = *response;
    PS_VLOG(kOriginated, log_context_) << "\nGetBidsResponse:\n"
                                       << found_response->DebugString();
    if (found_response->has_debug_info()) {
      server_common::DebugInfo& bfe_log =
          *response_->mutable_debug_info()->add_downstream_servers();
      bfe_log = std::move(*found_response->mutable_debug_info());
      bfe_log.set_server_name(buyer_ig_owner);
    }
    if ((!is_protected_audience_enabled_ || found_response->bids().empty()) &&
        (!is_pas_enabled_ ||
         found_response->protected_app_signals_bids().empty())) {
      PS_VLOG(kNoisyWarn, log_context_) << "Skipping buyer " << buyer_ig_owner
                                        << " due to empty GetBidsResponse.";

      async_task_tracker_.TaskCompleted(
          TaskStatus::EMPTY_RESPONSE,
          [this, &buyer_ig_owner, response = *std::move(response)]() mutable {
            RecordInterestGroupUpdates(buyer_ig_owner, shared_ig_updates_map_,
                                       *response);
          });
    } else {
      async_task_tracker_.TaskCompleted(
          TaskStatus::SUCCESS,
          [this, &buyer_ig_owner, response = *std::move(response)]() mutable {
            RecordInterestGroupUpdates(buyer_ig_owner, shared_ig_updates_map_,
                                       *response);
            shared_buyer_bids_map_.try_emplace(buyer_ig_owner,
                                               std::move(response));
          });
    }
  } else {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeGetBidsResponseError));
    LogInitiatedRequestErrorMetrics(metric::kBfe, response.status(),
                                    buyer_ig_owner);
    PS_LOG(ERROR, log_context_)
        << "GetBidsRequest failed for buyer " << buyer_ig_owner
        << "\nresponse status: " << response.status();

    async_task_tracker_.TaskCompleted(TaskStatus::ERROR);
  }
}

void SelectAdReactor::OnAllBidsDone(bool any_successful_bids) {
  if (enable_cancellation_ && request_context_->IsCancelled()) {
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }
  // No successful bids received.
  if (shared_buyer_bids_map_.empty()) {
    PS_VLOG(kNoisyWarn, log_context_) << kNoBidsReceived;

    if (!any_successful_bids) {
      LogIfError(
          metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
              1, metric::kSfeSelectAdNoSuccessfulBid));
      PS_LOG(WARNING, log_context_)
          << "Finishing the SelectAdRequest RPC with an error";

      FinishWithStatus(grpc::Status(grpc::INTERNAL, kInternalServerError));
      return;
    }
    // Since no buyers have returned bids, we would still finish the call RPC
    // call here and send a chaff back.
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }

  // Must fulfill preconditions:
  // That the shared_buyer_bids_map_ is non-empty was just checked above
  // That each buyer has bids was checked in OnFetchBidsDone().
  // Thus the preconditions of the following method are satisfied.
  if (!FilterBidsWithMismatchingCurrency()) {
    PS_VLOG(kNoisyWarn, log_context_) << kAllBidsRejectedBuyerCurrencyMismatch;
    FinishWithStatus(grpc::Status(grpc::INVALID_ARGUMENT,
                                  kAllBidsRejectedBuyerCurrencyMismatch));
  } else if (perform_scoring_signals_fetch_) {
    if (enable_enforce_kanon_) {
      executor_->Run([this]() {
        // PopulateKAnonStatusForBids is called from within the following
        // method when k-anon is enabled.
        QueryKAnonHashes();
      });
    } else {
      // If k-anon is not enforced, we still unconditionally find the
      // k-anon status for each bid before sending it for scoring and hence
      // have to make sure we call this method.
      PopulateKAnonStatusForBids();
    }
    FetchScoringSignals();
  } else if (enable_enforce_kanon_) {
    QueryKAnonHashes();
  } else {
    PopulateKAnonStatusForBids();
    ScoreAds();
  }
}

template <typename T>
void SelectAdReactor::FilterBidsWithMismatchingCurrencyHelper(
    google::protobuf::RepeatedPtrField<T>* ads_with_bids,
    absl::string_view buyer_currency) {
  int i = 0;
  int remove_starting_at = ads_with_bids->size();
  while (i < remove_starting_at) {
    const T& ad_with_bid = (*ads_with_bids)[i];
    if (!ad_with_bid.bid_currency().empty() &&
        buyer_currency != ad_with_bid.bid_currency()) {
      // Swap to last. Mark for removal and make sure we don't check it again
      ads_with_bids->SwapElements(i, --remove_starting_at);
      LogIfError(
          metric_context_->AccumulateMetric<metric::kAuctionBidRejectedCount>(
              1, ToSellerRejectionReasonString(
                     SellerRejectionReason::
                         BID_FROM_GENERATE_BID_FAILED_CURRENCY_CHECK)));
      // Leave index un-incremented so swapped element is checked.
    } else {
      i++;
    }
  }
  // Delete all mismatched pas_bids.
  if (remove_starting_at < ads_with_bids->size()) {
    ads_with_bids->DeleteSubrange(remove_starting_at,
                                  ads_with_bids->size() - remove_starting_at);
  }
}

bool SelectAdReactor::FilterBidsWithMismatchingCurrency() {
  DCHECK(!shared_buyer_bids_map_.empty())
      << "PRECONDITION 1: shared_buyer_bids_map_ must have at least one entry.";
  DCHECK(([shared_buyer_bids_map_ptr = &(shared_buyer_bids_map_),
           is_protected_audience_enabled = is_protected_audience_enabled_,
           is_pas_enabled = is_pas_enabled_]() {
    for (const auto& [buyer_ig_owner, get_bid_response] :
         *shared_buyer_bids_map_ptr) {
      // Check if no bids are present.
      if ((!is_protected_audience_enabled ||
           get_bid_response->bids().empty()) &&
          (!is_pas_enabled ||
           get_bid_response->protected_app_signals_bids().empty())) {
        return false;
      }
    }
    return true;
  }()))
      << "PRECONDITION 2: each buyer in shared_buyer_bids_map must be "
         "non-empty.";
  bool any_valid_bids = false;
  // Check that each AdWithBid's currency is as-expected.
  // Throw out the AdWithBids which do not match.
  for (auto& [buyer_ig_owner, get_bids_raw_response] : shared_buyer_bids_map_) {
    // It is possible for a buyer to have no buyer_config.
    const auto& buyer_config_itr =
        auction_config_.per_buyer_config().find(buyer_ig_owner);
    if (buyer_config_itr == auction_config_.per_buyer_config().end()) {
      // Preconditions mean bids must be present.
      any_valid_bids = true;
      continue;
    }
    // Not all buyers have an expected currency specified.
    absl::string_view buyer_currency =
        buyer_config_itr->second.buyer_currency();
    if (buyer_currency.empty()) {
      // Preconditions mean bids must be present.
      any_valid_bids = true;
      continue;
    }

    FilterBidsWithMismatchingCurrencyHelper<AdWithBid>(
        get_bids_raw_response->mutable_bids(), buyer_currency);
    FilterBidsWithMismatchingCurrencyHelper<ProtectedAppSignalsAdWithBid>(
        get_bids_raw_response->mutable_protected_app_signals_bids(),
        buyer_currency);

    // Check if any bids remain.
    if ((is_protected_audience_enabled_ &&
         !get_bids_raw_response->bids().empty()) ||
        (is_pas_enabled_ &&
         !get_bids_raw_response->protected_app_signals_bids().empty())) {
      any_valid_bids = true;
    }
  }
  // Clear buyers with no remaining bids.
  absl::erase_if(
      shared_buyer_bids_map_,
      [is_p_aud_enabled = is_protected_audience_enabled_,
       is_pas_enabled = is_pas_enabled_](const auto& buyer_to_bids_pair) {
        const auto& get_bids_raw_response = buyer_to_bids_pair.second;
        // Check if any bids remain.
        return ((!is_p_aud_enabled || get_bids_raw_response->bids().empty()) &&
                (!is_pas_enabled ||
                 get_bids_raw_response->protected_app_signals_bids().empty()));
      });
  return any_valid_bids;
}

void SelectAdReactor::CancellableFetchScoringSignalsV1(
    const ScoringSignalsRequest& scoring_signals_request) {
  if (!perform_scoring_signals_fetch_) {
    FinishWithStatus(grpc::Status(grpc::StatusCode::INTERNAL,
                                  kCheckSignalsFetchFlagV1ErrorMsg));
    return;
  }
  if (clients_.scoring_signals_async_provider == nullptr) {
    FinishWithStatus(grpc::Status(grpc::StatusCode::INTERNAL,
                                  kCheckProviderNullnessV1ErrorMsg));
    return;
  }
  auto kv_request =
      metric::MakeInitiatedRequest(metric::kKv, metric_context_.get())
          .release();
  clients_.scoring_signals_async_provider->Get(
      scoring_signals_request,
      CancellationWrapper(
          request_context_, enable_cancellation_,
          [this, kv_request](
              absl::StatusOr<std::unique_ptr<ScoringSignals>> result,
              GetByteSize get_byte_size) mutable {
            {
              // Only logs KV request and response sizes if fetching signals
              // succeeds.
              if (result.ok()) {
                kv_request->SetRequestSize(
                    static_cast<int>(get_byte_size.request));
                kv_request->SetResponseSize(
                    static_cast<int>(get_byte_size.response));
              }
              // destruct kv_request, destructor measures request time
              delete kv_request;
            }
            maybe_scoring_signals_ = std::move(result);
            fetch_scoring_signals_query_kanon_tracker_.TaskCompleted(
                TaskStatus::SUCCESS);
          },
          [this, kv_request]() {
            delete kv_request;
            fetch_scoring_signals_query_kanon_tracker_.TaskCompleted(
                TaskStatus::CANCELLED);
          }),
      absl::Milliseconds(config_client_.GetIntParameter(
          KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS)),
      {log_context_});
}

void SelectAdReactor::CancellableFetchScoringSignalsV2(
    const ScoringSignalsRequest& scoring_signals_request) {
  if (!perform_scoring_signals_fetch_) {
    FinishWithStatus(grpc::Status(grpc::StatusCode::INTERNAL,
                                  kCheckSignalsFetchFlagV2ErrorMsg));
    return;
  }
  if (clients_.kv_async_client == nullptr) {
    FinishWithStatus(grpc::Status(grpc::StatusCode::INTERNAL,
                                  kCheckProviderNullnessV2ErrorMsg));
    return;
  }
  auto kv_request =
      metric::MakeInitiatedRequest(metric::kKv, metric_context_.get())
          .release();
  std::optional<server_common::ConsentedDebugConfiguration>
      consented_debug_config = std::nullopt;
  std::visit(
      [&consented_debug_config](const auto& protected_auction_input) {
        if (protected_auction_input.has_consented_debug_config()) {
          consented_debug_config =
              protected_auction_input.consented_debug_config();
        }
      },
      protected_auction_input_);
  auto maybe_scoring_signals_request = CreateV2ScoringRequest(
      scoring_signals_request, is_pas_enabled_, consented_debug_config);
  if (!maybe_scoring_signals_request.ok()) {
    PS_VLOG(kNoisyWarn, log_context_) << "Failed creating TKV scoring request. "
                                      << maybe_scoring_signals_request.status();
    return;
  }

  std::visit(
      [this,
       &maybe_scoring_signals_request](const auto& protected_auction_input) {
        server_common::LogContext* log_context =
            (*maybe_scoring_signals_request)->mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(auction_config_.seller_debug_id());
      },
      protected_auction_input_);

  grpc::ClientContext* client_context = client_contexts_.Add();

  EventMessage::KvSignal score_signal = KvEventMessage(
      (*maybe_scoring_signals_request)->ShortDebugString(), log_context_);

  auto status = clients_.kv_async_client->ExecuteInternal(
      *std::move(maybe_scoring_signals_request), client_context,
      CancellationWrapper(
          request_context_, enable_cancellation_,
          [this, kv_request, score_signal = std::move(score_signal)](
              KVLookUpResult kv_look_up_result,
              ResponseMetadata response_metadata) mutable {
            {
              // Only logs KV request and response sizes if fetching signals
              // succeeds.
              if (kv_look_up_result.ok()) {
                kv_request->SetRequestSize(response_metadata.request_size);
                kv_request->SetResponseSize(response_metadata.response_size);
              }
              // destruct kv_request, destructor measures request time
              delete kv_request;
            }
            if (!kv_look_up_result.ok()) {
              LogIfError(
                  metric_context_
                      ->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
                          1, metric::kSfeScoringSignalsResponseError));
              LogInitiatedRequestErrorMetrics(metric::kKv,
                                              kv_look_up_result.status());
              PS_LOG(ERROR, log_context_)
                  << "Scoring signals fetch from key-value server failed: "
                  << kv_look_up_result.status();
              ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kInternalError,
                          ErrorCode::SERVER_SIDE);
              OnScoreAdsDone(
                  std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
              if (server_common::log::PS_VLOG_IS_ON(kKVLog)) {
                log_context_.SetEventMessageField(std::move(score_signal));
              }
              return;
            }
            KVV2AdapterStats v2_adapter_stats;
            auto signals = ConvertV2ResponseToV1ScoringSignals(
                *std::move(kv_look_up_result), v2_adapter_stats);
            PS_VLOG(kStats, log_context_)
                << "Number of values with additional json string parsing "
                   "applied:"
                << v2_adapter_stats.values_with_json_string_parsing
                << " and without additional json string parsing applied:"
                << v2_adapter_stats.values_without_json_string_parsing;

            SetKvEventMessage("KVAsyncGrpcClient",
                              *((*signals)->scoring_signals),
                              std::move(score_signal), log_context_);
            maybe_scoring_signals_ = *std::move(signals);
            fetch_scoring_signals_query_kanon_tracker_.TaskCompleted(
                TaskStatus::SUCCESS);
          },
          [this, kv_request]() {
            delete kv_request;
            FinishWithStatus(
                grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
          }),
      absl::Milliseconds(config_client_.GetIntParameter(
          KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS)));
}

void SelectAdReactor::CancellableFetchScoringSignals() {
  ScoringSignalsRequest scoring_signals_request(
      shared_buyer_bids_map_, buyer_metadata_, request_->client_type());
  if (auction_config_.has_code_experiment_spec() &&
      auction_config_.code_experiment_spec()
          .has_seller_kv_experiment_group_id()) {
    scoring_signals_request.seller_kv_experiment_group_id_ = absl::StrCat(
        auction_config_.code_experiment_spec().seller_kv_experiment_group_id());
  }
  absl::string_view kv_v2_signals_host =
      config_client_.GetStringParameter(TRUSTED_KEY_VALUE_V2_SIGNALS_HOST);
  if (UseKvV2(request_->client_type(), is_tkv_v2_browser_enabled_,
              config_client_.GetBooleanParameter(TEST_MODE),
              kv_v2_signals_host.empty() ||
                  kv_v2_signals_host == kIgnoredPlaceholderValue)) {
    CancellableFetchScoringSignalsV2(scoring_signals_request);
  } else {
    CancellableFetchScoringSignalsV1(scoring_signals_request);
  }
}

void SelectAdReactor::OnFetchScoringSignalsDone(
    absl::StatusOr<std::unique_ptr<ScoringSignals>> result) {
  if (!perform_scoring_signals_fetch_) {
    ScoreAds();
    return;
  }

  if (!result.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeScoringSignalsResponseError));
    LogInitiatedRequestErrorMetrics(metric::kKv, result.status());
    PS_LOG(ERROR, log_context_)
        << "Scoring signals fetch from key-value server failed: "
        << result.status();

    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kInternalError,
                ErrorCode::SERVER_SIDE);
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  scoring_signals_ = std::move(result).value();
  // If signals are empty, return chaff.
  if (scoring_signals_->scoring_signals->empty()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Scoring signals fetch from key-value server "
           "succeeded but were empty.";

    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  ScoreAds();
}

std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
SelectAdReactor::CreateScoreAdsRequest() {
  auto raw_request = std::make_unique<ScoreAdsRequest::ScoreAdsRawRequest>();
  if (is_protected_audience_enabled_) {
    // If protected audience support is not enabled then buyers should not
    // be returning bids anyway for Protected Audience but this check is placed
    // here for safety as well as efficiency.
    for (const auto& [buyer_ig_owner, get_bid_response] :
         shared_buyer_bids_map_) {
      for (const AdWithBid& ad_with_bid : get_bid_response->bids()) {
        raw_request->mutable_ad_bids()->Add(BuildAdWithBidMetadata(
            ad_with_bid, buyer_ig_owner, bid_k_anon_status_[&ad_with_bid]));
      }
    }
  }
  *raw_request->mutable_auction_signals() = auction_config_.auction_signals();
  raw_request->set_seller_currency(auction_config_.seller_currency());
  *raw_request->mutable_seller_signals() = auction_config_.seller_signals();
  raw_request->set_top_level_seller(auction_config_.top_level_seller());
  raw_request->set_score_ad_version(
      auction_config_.code_experiment_spec().score_ad_version());
  if (scoring_signals_ != nullptr) {
    // Ad scoring signals cannot be used after this.
    raw_request->set_allocated_scoring_signals(
        scoring_signals_->scoring_signals.release());
    raw_request->set_seller_data_version(scoring_signals_->data_version);
  }
  std::visit(
      [&raw_request, this](const auto& protected_auction_input) {
        raw_request->set_publisher_hostname(
            protected_auction_input.publisher_name());
        raw_request->set_enable_debug_reporting(
            protected_auction_input.enable_debug_reporting());
        raw_request->mutable_fdo_flags()->set_enable_sampled_debug_reporting(
            protected_auction_input.fdo_flags()
                .enable_sampled_debug_reporting());
        raw_request->mutable_fdo_flags()->set_in_cooldown_or_lockout(
            protected_auction_input.fdo_flags().in_cooldown_or_lockout());
        auto* log_context = raw_request->mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(auction_config_.seller_debug_id());
        if (protected_auction_input.has_consented_debug_config()) {
          *raw_request->mutable_consented_debug_config() =
              protected_auction_input.consented_debug_config();
        }
        if (enable_enforce_kanon_) {
          raw_request->set_num_allowed_ghost_winners(
              kNumAllowedChromeGhostWinners);
          raw_request->set_enforce_kanon(
              protected_auction_input.enforce_kanon());
        }
      },
      protected_auction_input_);

  for (const auto& [buyer, per_buyer_config] :
       auction_config_.per_buyer_config()) {
    raw_request->mutable_per_buyer_signals()->try_emplace(
        buyer, per_buyer_config.buyer_signals());
  }
  raw_request->set_seller(auction_config_.seller());
  raw_request->set_is_sampled_for_debug(is_sampled_for_debug_);
  return raw_request;
}

void SelectAdReactor::CancellableScoreAds() {
  auto raw_request = CreateScoreAdsRequest();
  if (raw_request->ad_bids().empty() &&
      raw_request->protected_app_signals_ad_bids().empty()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "No Protected Audience or Protected App "
           "Signals ads to score, sending chaff response";
    OnScoreAdsDone(std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>());
    return;
  }
  PS_VLOG(kOriginated, log_context_) << "ScoreAdsRawRequest:\n"
                                     << raw_request->DebugString();

  auto auction_request =
      metric::MakeInitiatedRequest(metric::kAs, metric_context_.get())
          .release();
  auction_request->SetRequestSize((int)raw_request->ByteSizeLong());
  auto on_scoring_done = CancellationWrapper(
      request_context_, enable_cancellation_,
      [this, auction_request](
          absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
              result,
          ResponseMetadata response_metadata) mutable {
        {
          int response_size =
              result.ok() ? (int)result->get()->ByteSizeLong() : 0;
          auction_request->SetResponseSize(response_size);
          // destruct auction_request, destructor measures request time
          delete auction_request;
        }
        OnScoreAdsDone(std::move(result));
      },
      [this, auction_request]() {
        // Do not log request/response size metrics for cancelled requests.
        delete auction_request;
        FinishWithStatus(
            grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
      });

  grpc::ClientContext* client_context = client_contexts_.Add();

  absl::Status execute_result = clients_.scoring.ExecuteInternal(
      std::move(raw_request), client_context, std::move(on_scoring_done),
      absl::Milliseconds(
          config_client_.GetIntParameter(SCORE_ADS_RPC_TIMEOUT_MS)));
  if (!execute_result.ok()) {
    delete auction_request;
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeScoreAdsFailedToCall));
    PS_LOG(ERROR, log_context_)
        << absl::StrFormat("Failed to make async ScoreAds call: (error: %s)",
                           execute_result.ToString());
    FinishWithStatus(grpc::Status(grpc::INTERNAL, kInternalServerError));
  }
}

void SelectAdReactor::FinishWithStatus(const grpc::Status& status) {
  if (status.error_code() != grpc::StatusCode::OK) {
    PS_LOG(ERROR, log_context_) << "RPC failed: " << status.error_message();
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  }
  if (metric_context_->CustomState(kWinningAd).ok()) {
    LogIfError(metric_context_->LogHistogram<metric::kSfeWithWinnerTimeMs>(
        static_cast<int>((absl::Now() - start_) / absl::Milliseconds(1))));
  }
  benchmarking_logger_->End();
  log_context_.ExportEventMessage(/*if_export_consented=*/true,
                                  should_export_debug_);
  Finish(status);
}

void SelectAdReactor::OnScoreAdsDone(
    absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
        response) {
  std::optional<AdScore> high_score;
  const AdScores* ghost_winners = nullptr;
  if (HaveAdServerVisibleErrors()) {
    PS_LOG(WARNING, log_context_)
        << "Finishing the SelectAdRequest RPC with ad server visible error";

    FinishWithStatus(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                  error_accumulator_.GetAccumulatedErrorString(
                                      ErrorVisibility::AD_SERVER_VISIBLE)));
    return;
  }

  PS_VLOG(kOriginated, log_context_)
      << "ScoreAdsResponse status:" << response.status();
  auto scoring_return_code =
      static_cast<grpc::StatusCode>(response.status().code());
  if (!response.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
            1, metric::kSfeScoreAdsResponseError));
    LogInitiatedRequestErrorMetrics(metric::kAs, response.status());
    benchmarking_logger_->End();
    // Any INTERNAL errors from auction service will be suppressed by SFE and
    // will cause a chaff to be sent back. Non-INTERNAL errors on the other hand
    // are propagated back the seller ad service.
    if (scoring_return_code != grpc::StatusCode::INTERNAL) {
      FinishWithStatus(grpc::Status(scoring_return_code,
                                    std::string(response.status().message())));
      return;
    }
  }

  if (scoring_return_code == grpc::StatusCode::OK) {
    const auto& found_response = *response;
    should_export_debug_ = found_response->auction_export_debug();
    if (found_response->has_ad_score() &&
        found_response->ad_score().buyer_bid() > 0) {
      high_score = found_response->ad_score();
      LogIfError(
          metric_context_->LogUpDownCounter<metric::kRequestWithWinnerCount>(
              1));
      metric_context_->SetCustomState(kWinningAd, "");
    }
    if (found_response->has_debug_info()) {
      server_common::DebugInfo& auction_log =
          *response_->mutable_debug_info()->add_downstream_servers();
      auction_log = std::move(*found_response->mutable_debug_info());
      auction_log.set_server_name("auction");
    }
    if (!found_response->ghost_winning_ad_scores().empty()) {
      if (enable_buyer_private_aggregate_reporting_) {
        for (auto& ghost_winning_ad_score :
             *found_response->mutable_ghost_winning_ad_scores()) {
          HandlePrivateAggregationContributionsForGhostWinner(
              interest_group_index_map_, ghost_winning_ad_score,
              shared_buyer_bids_map_);
        }
      }
      ghost_winners = &(found_response->ghost_winning_ad_scores());
    }
    PerformDebugReporting(found_response->mutable_seller_debug_reports(),
                          high_score);
    if (high_score && enable_buyer_private_aggregate_reporting_) {
      HandlePrivateAggregationContributions(
          interest_group_index_map_, *high_score, shared_buyer_bids_map_);
    }
  }

  std::optional<AuctionResult::Error> error;
  if (HaveClientVisibleErrors()) {
    error = std::move(error_);
  }
  absl::StatusOr<std::string> non_encrypted_response = GetNonEncryptedResponse(
      high_score, error, ghost_winners, per_adtech_paapi_contributions_limit_);
  if (!non_encrypted_response.ok()) {
    FinishWithStatus(grpc::Status(grpc::INTERNAL, kInternalServerError));
    return;
  }

  if (!EncryptResponse(*std::move(non_encrypted_response))) {
    return;
  }

  PS_VLOG(kEncrypted, log_context_)
      << "SelectAdResponse.auction_result_ciphertext base64:\n"
      << absl::Base64Escape(response_->auction_result_ciphertext());

  PS_VLOG(kEncrypted, log_context_) << "Encrypted SelectAdResponse:\n"
                                    << response_->ShortDebugString();

  FinishWithStatus(grpc::Status::OK);
}

DecodedBuyerInputs SelectAdReactor::GetDecodedBuyerinputs(
    const EncodedBuyerInputs& encoded_buyer_inputs) {
  return DecodeBuyerInputs(encoded_buyer_inputs, error_accumulator_,
                           fail_fast_);
}

bool SelectAdReactor::EncryptResponse(std::string plaintext_response) {
  absl::StatusOr<std::string> encapsulated_response;
  if (auction_scope_ ==
      AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
    // If this will be decrypted by another SFE, encrypt with public key.
    auto encrypted_request =
        HpkeEncrypt(plaintext_response, *clients_.crypto_client_ptr_,
                    clients_.key_fetcher_manager_,
                    ProtoCloudPlatformToScpCloudPlatform(
                        auction_config_.top_level_cloud_platform()));
    if (!encrypted_request.ok()) {
      PS_LOG(ERROR, log_context_) << "Error while encrypting response: "
                                  << encrypted_request.status().message();
      FinishWithStatus(
          grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
      return false;
    }
    encapsulated_response = std::move(encrypted_request->ciphertext);
    *response_->mutable_key_id() = std::move(encrypted_request->key_id);
  } else {
    // Decrypted by device -> Encrypt with corresponding private key.
    encapsulated_response = server_common::EncryptAndEncapsulateResponse(
        std::move(plaintext_response), decrypted_request_->private_key,
        decrypted_request_->context, decrypted_request_->request_label);
  }

  if (!encapsulated_response.ok()) {
    PS_LOG(ERROR, log_context_)
        << absl::StrFormat("Error during response encryption/encapsulation: %s",
                           encapsulated_response.status().message());

    FinishWithStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, kInternalServerError));
    return false;
  }

  response_->mutable_auction_result_ciphertext()->assign(
      std::move(*encapsulated_response));
  return true;
}

void SelectAdReactor::PerformDebugReporting(
    DebugReports* seller_debug_reports,
    const std::optional<AdScore>& high_score) {
  // Create new metric context.
  metric::MetricContextMap<SelectAdRequest>()->Get(request_);
  // Make metric_context a unique_ptr by releasing the ownership of the context
  // from ContextMap.
  absl::StatusOr<std::unique_ptr<metric::SfeContext>> metric_context =
      metric::MetricContextMap<SelectAdRequest>()->Remove(request_);
  PS_CHECK_OK(metric_context, log_context_);
  std::shared_ptr<metric::SfeContext> shared_context =
      *std::move(metric_context);

  bool enable_debug_reporting = false;
  bool enable_sampled_debug_reporting = false;
  std::visit(
      [&enable_debug_reporting,
       &enable_sampled_debug_reporting](const auto& protected_auction_input) {
        enable_debug_reporting =
            protected_auction_input.enable_debug_reporting();
        enable_sampled_debug_reporting = protected_auction_input.fdo_flags()
                                             .enable_sampled_debug_reporting();
      },
      protected_auction_input_);
  if (!enable_debug_reporting) {
    return;
  }

  // Perform debug reporting for buyers.
  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      high_score, auction_config_.seller_currency(),
      (scoring_signals_ == nullptr) ? 0 : scoring_signals_->data_version);
  if (enable_sampled_debug_reporting) {
    PerformSampledDebugReporting(post_auction_signals);
  } else {
    PerformUnsampledDebugReporting(post_auction_signals, high_score);
  }

  // Merge debug reports for sellers into the adtech_origin_debug_urls_map in
  // the response.
  MergeSellerDebugReportsToResponse(seller_debug_reports,
                                    enable_sampled_debug_reporting);
}

void SelectAdReactor::PerformSampledDebugReporting(
    const PostAuctionSignals& post_auction_signals) {
  if (IsComponentAuction(auction_scope_)) {
    PerformSampledDebugReportingForComponentAuction(post_auction_signals);
    return;
  }

  // Single-seller auction. Iterate over all buyers.
  for (const auto& [ig_owner, get_bid_response] : shared_buyer_bids_map_) {
    bool put_buyer_in_cooldown_for_debug_win = false;
    bool put_buyer_in_cooldown_for_debug_loss = false;
    bool sampled_debug_url = false;

    // Iterate over all ads.
    for (const AdWithBid& ad_with_bid : get_bid_response->bids()) {
      absl::string_view ig_name = ad_with_bid.interest_group_name();
      bool is_winning_ig = (ig_owner == post_auction_signals.winning_ig_owner &&
                            ig_name == post_auction_signals.winning_ig_name);

      // Case 1. Debug url was not selected during sampling. The adtech needs to
      // be put in cooldown for calling the forDebuggingOnly API.
      if (is_winning_ig && ad_with_bid.debug_win_url_failed_sampling()) {
        put_buyer_in_cooldown_for_debug_win = true;
        continue;
      }
      if (!is_winning_ig && ad_with_bid.debug_loss_url_failed_sampling()) {
        put_buyer_in_cooldown_for_debug_loss = true;
        continue;
      }

      // Consider the debug win url for the winning ad, and the debug loss url
      // for the losing ad.
      absl::string_view debug_url =
          is_winning_ig
              ? ad_with_bid.debug_report_urls().auction_debug_win_url()
              : ad_with_bid.debug_report_urls().auction_debug_loss_url();
      if (debug_url.empty()) {
        continue;
      }

      // Case 2. Debug url passed sampling and is added to the response. Skip
      // all other ads for this buyer since there is a limit of one debug url
      // per adtech in single-seller auctions.
      AddBuyerDebugReportToResponse(
          debug_url, ig_owner, /*is_win_report=*/is_winning_ig,
          /*is_component_win=*/false,
          GetPlaceholderDataForInterestGroup(ig_owner, ig_name,
                                             post_auction_signals));
      sampled_debug_url = true;
      break;
    }
    if (sampled_debug_url) {
      continue;
    }

    // If no debug url was selected during sampling, add a corresponding debug
    // report with an empty url to pass cooldown information to the client.
    if (put_buyer_in_cooldown_for_debug_win) {
      AddBuyerDebugReportToResponse("", ig_owner, /*is_win_report=*/true,
                                    /*is_component_win=*/false);
    } else if (put_buyer_in_cooldown_for_debug_loss) {
      AddBuyerDebugReportToResponse("", ig_owner, /*is_win_report=*/false,
                                    /*is_component_win=*/false);
    }
  }
}

void SelectAdReactor::PerformSampledDebugReportingForComponentAuction(
    const PostAuctionSignals& post_auction_signals) {
  // Iterate over all buyers.
  for (const auto& [ig_owner, get_bid_response] : shared_buyer_bids_map_) {
    // Early exit condition variables:
    //
    // 1. Handled the debug win url for the component auction winning ad.
    bool win_url_checked = false;
    // 2. Found a debug loss url that was selected during sampling and added to
    // the response (there is a limit of max one debug loss url per adtech).
    bool loss_url_selected = false;
    // 3. Found a debug loss url for a losing ad, guaranteeing that the adtech
    // needs to be put in cooldown.

    // Iterate over all ads.
    bool put_buyer_in_cooldown_for_confirmed_loss = false;
    for (const AdWithBid& ad_with_bid : get_bid_response->bids()) {
      // Early exit for current buyer.
      if (win_url_checked && loss_url_selected &&
          put_buyer_in_cooldown_for_confirmed_loss) {
        break;
      }
      absl::string_view ig_name = ad_with_bid.interest_group_name();
      bool is_winning_ig = (ig_owner == post_auction_signals.winning_ig_owner &&
                            ig_name == post_auction_signals.winning_ig_name);

      // Handle debug win url for component auction winning ad.
      if (is_winning_ig) {
        HandleSampledDebugWinReportForComponentAuctionWinner(
            ad_with_bid, post_auction_signals);
        win_url_checked = true;
      }

      // Handle debug loss url for all ads (since component auction winning ad
      // can still lose in the top-level auction).
      HandleSampledDebugLossReportForComponentAuctionAd(
          ad_with_bid, ig_owner, ig_name, post_auction_signals, is_winning_ig,
          loss_url_selected, put_buyer_in_cooldown_for_confirmed_loss);
    }

    // If a confirmed debug loss url was selected during sampling, add a
    // corresponding debug report with an empty url to pass cooldown information
    // to the client.
    if (put_buyer_in_cooldown_for_confirmed_loss) {
      AddBuyerDebugReportToResponse("", ig_owner, /*is_win_report=*/false,
                                    /*is_component_win=*/false);
    }
  }
}

void SelectAdReactor::HandleSampledDebugWinReportForComponentAuctionWinner(
    const AdWithBid& winning_ad,
    const PostAuctionSignals& post_auction_signals) {
  absl::string_view ig_owner = post_auction_signals.winning_ig_owner;
  absl::string_view ig_name = post_auction_signals.winning_ig_name;
  absl::string_view debug_win_url =
      winning_ad.debug_report_urls().auction_debug_win_url();

  // Add the sampled debug win url to the response.
  if (!debug_win_url.empty()) {
    AddBuyerDebugReportToResponse(debug_win_url, ig_owner,
                                  /*is_win_report=*/true,
                                  /*is_component_win=*/true,
                                  GetPlaceholderDataForInterestGroup(
                                      ig_owner, ig_name, post_auction_signals));
    return;
  }

  // If not selected, add a corresponding debug report with an empty url to
  // pass the cooldown information to the client.
  if (winning_ad.debug_win_url_failed_sampling()) {
    AddBuyerDebugReportToResponse("", ig_owner, /*is_win_report=*/true,
                                  /*is_component_win=*/true);
  }
}

void SelectAdReactor::HandleSampledDebugLossReportForComponentAuctionAd(
    const AdWithBid& ad, absl::string_view ig_owner, absl::string_view ig_name,
    const PostAuctionSignals& post_auction_signals, bool is_winning_ig,
    bool& loss_url_selected, bool& put_buyer_in_cooldown_for_confirmed_loss) {
  absl::string_view debug_loss_url =
      ad.debug_report_urls().auction_debug_loss_url();
  if (!ad.debug_loss_url_failed_sampling() && debug_loss_url.empty()) {
    return;
  }

  // If no debug loss url has been selected yet, and this one passed sampling,
  // add this to the response. No more debug loss urls can be selected now.
  if (!loss_url_selected && !debug_loss_url.empty()) {
    AddBuyerDebugReportToResponse(debug_loss_url, ig_owner,
                                  /*is_win_report=*/false,
                                  /*is_component_win=*/is_winning_ig,
                                  GetPlaceholderDataForInterestGroup(
                                      ig_owner, ig_name, post_auction_signals));
    loss_url_selected = true;
    return;
  }

  // If this debug loss url is for the component auction winning ad and did
  // not get selected, add a corresponding debug report with an empty url to the
  // response. This will enable the client to put the adtech in cooldown if this
  // ad goes on to lose in the top-level auction.
  if (is_winning_ig) {
    AddBuyerDebugReportToResponse("", ig_owner, /*is_win_report=*/false,
                                  /*is_component_win=*/true);
    return;
  }

  // If this debug loss url is for a losing ad and did not get selected, this
  // is a confirmed loss url that will put the adtech in cooldown.
  put_buyer_in_cooldown_for_confirmed_loss = true;
}

void SelectAdReactor::PerformUnsampledDebugReporting(
    const PostAuctionSignals& post_auction_signals,
    const std::optional<AdScore>& high_score) {
  // Iterate over all buyers and their ads.
  for (const auto& [ig_owner, get_bid_response] : shared_buyer_bids_map_) {
    for (const AdWithBid& ad_with_bid : get_bid_response->bids()) {
      if (!ad_with_bid.has_debug_report_urls()) {
        continue;
      }
      absl::string_view ig_name = ad_with_bid.interest_group_name();
      bool is_winning_ig = (ig_owner == post_auction_signals.winning_ig_owner &&
                            ig_name == post_auction_signals.winning_ig_name);

      // For component auction winning ad, skip buyer debug pings and instead
      // add both win and loss debug reports to the response.
      if (is_winning_ig && IsComponentAuction(auction_scope_)) {
        PerformUnsampledDebugReportingForComponentAuctionWinner(
            ad_with_bid, post_auction_signals);
        continue;
      }

      // For all other ads, send debug pings from the server.
      absl::string_view debug_url =
          is_winning_ig
              ? ad_with_bid.debug_report_urls().auction_debug_win_url()
              : ad_with_bid.debug_report_urls().auction_debug_loss_url();
      SendDebugReportingPing(*clients_.reporting, debug_url, ig_owner, ig_name,
                             is_winning_ig, post_auction_signals);
    }
  }
}

void SelectAdReactor::PerformUnsampledDebugReportingForComponentAuctionWinner(
    const AdWithBid& winning_ad,
    const PostAuctionSignals& post_auction_signals) {
  absl::string_view ig_owner = post_auction_signals.winning_ig_owner;
  DebugReportingPlaceholder placeholder_data =
      GetPlaceholderDataForInterestGroup(
          ig_owner, winning_ad.interest_group_name(), post_auction_signals);

  // Add debug win url for component auction winning ad to response if present.
  absl::string_view debug_win_url =
      winning_ad.debug_report_urls().auction_debug_win_url();
  if (!debug_win_url.empty()) {
    AddBuyerDebugReportToResponse(debug_win_url, ig_owner,
                                  /*is_win_report=*/true,
                                  /*is_component_win=*/true, placeholder_data);
  }

  // Add debug loss url for component auction winning ad to response if present.
  absl::string_view debug_loss_url =
      winning_ad.debug_report_urls().auction_debug_loss_url();
  if (!debug_loss_url.empty()) {
    AddBuyerDebugReportToResponse(debug_loss_url, ig_owner,
                                  /*is_win_report=*/false,
                                  /*is_component_win=*/true, placeholder_data);
  }
}

void SelectAdReactor::AddBuyerDebugReportToResponse(
    absl::string_view debug_url, absl::string_view adtech_origin,
    bool is_win_report, bool is_component_win,
    std::optional<DebugReportingPlaceholder> placeholder_data) {
  // The debug url is for the winning interest group if it is a debug win url,
  // or if it is a debug loss url for the component auction winner.
  bool is_winning_ig = is_win_report || is_component_win;

  // Add debug report object to the response.
  DebugReports& debug_reports = adtech_origin_debug_urls_map_[adtech_origin];
  auto* debug_report = debug_reports.add_reports();
  debug_report->set_url((placeholder_data.has_value())
                            ? CreateDebugReportingUrlForInterestGroup(
                                  debug_url, *placeholder_data, is_winning_ig)
                            : debug_url);
  debug_report->set_is_seller_report(false);
  debug_report->set_is_win_report(is_win_report);
  debug_report->set_is_component_win(is_component_win);
}

void SelectAdReactor::MergeSellerDebugReportsToResponse(
    DebugReports* seller_debug_reports, bool enable_sampled_debug_reporting) {
  if (!seller_debug_reports || seller_debug_reports->reports().empty()) {
    return;
  }

  // If the seller is also a buyer in the auction, there may be existing debug
  // reports for the adtech origin. Otherwise, an empty DebugReports object
  // is created.
  DebugReports& debug_reports =
      adtech_origin_debug_urls_map_[auction_config_.seller()];

  // Get counts for existing non-empty (buyer) debug reports.
  DebugReportsInResponseCounts debug_reports_counts =
      DebugReportsInResponseCounts::Create(enable_sampled_debug_reporting,
                                           IsComponentAuction(auction_scope_));
  for (auto& debug_report : debug_reports.reports()) {
    debug_reports_counts.DecrementCountsForDebugReport(debug_report);
  }

  // Add seller debug reports as long as they respect the max allowed counts per
  // adtech origin.
  for (auto& debug_report : *seller_debug_reports->mutable_reports()) {
    if (debug_reports_counts.CanAddDebugReport(debug_report)) {
      debug_reports_counts.DecrementCountsForDebugReport(debug_report);
      *debug_reports.add_reports() = std::move(debug_report);
    }
  }
}

void SelectAdReactor::OnDone() { delete this; }

void SelectAdReactor::OnCancel() {
  if (enable_cancellation_) {
    client_contexts_.CancelAll();
  }
}

void SelectAdReactor::ReportError(
    log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    const std::string& msg, ErrorCode error_code) {
  const auto& location = error_visibility_with_loc.location;
  ErrorVisibility error_visibility = error_visibility_with_loc.mandatory_param;
  error_accumulator_.ReportError(location, error_visibility, msg, error_code);
}

void SelectAdReactor::QueryKAnonHashes() {
  auto timer = std::make_unique<KAnonLatencyReporter>(metric_context_.get());
  bid_k_anon_hash_sets_ = GetKAnonHashesForBids();
  PS_VLOG(5, log_context_) << "Querying k-anon service for hashes";
  if (bid_k_anon_hash_sets_.empty()) {
    PS_VLOG(5, log_context_) << "No hashes to query from k-anon service";
    fetch_scoring_signals_query_kanon_tracker_.TaskCompleted(
        TaskStatus::SKIPPED);
    return;
  }

  absl::flat_hash_set<absl::string_view> query_sets;
  for (const auto& [unused_bid, hashes] : bid_k_anon_hash_sets_) {
    for (const auto& hash : hashes) {
      query_sets.insert(hash);
    }
  }
  if (query_sets.empty()) {
    PS_VLOG(5, log_context_)
        << "No unresolved k-anon hashes, will not query k-anon service";
    fetch_scoring_signals_query_kanon_tracker_.TaskCompleted(
        TaskStatus::SKIPPED);
    return;
  }
  absl::Status status = clients_.k_anon_cache_manager->AreKAnonymous(
      GetKAnonSetType(), std::move(query_sets),
      [this, timer = std::move(timer)](
          absl::StatusOr<absl::flat_hash_set<std::string>> response) {
        if (!response.ok()) {
          PS_LOG(ERROR, log_context_)
              << "k-anon cache manager returned an error: "
              << response.status();
          LogInitiatedRequestErrorMetrics(metric::kKAnon, response.status());
          fetch_scoring_signals_query_kanon_tracker_.TaskCompleted(
              TaskStatus::ERROR);
          return;
        }
        PS_VLOG(5, log_context_)
            << "k-anon cache manager returned a successful response";
        queried_k_anon_hashes_ = *std::move(response);
        PopulateKAnonStatusForBids();
        fetch_scoring_signals_query_kanon_tracker_.TaskCompleted(
            TaskStatus::SUCCESS);
      },
      metric_context_.get());
  if (!status.ok()) {
    LogInitiatedRequestErrorMetrics(metric::kKAnon, status);
    PS_LOG(ERROR, log_context_)
        << "Failed to query k-anon cache manager: " << status;
    PopulateKAnonStatusForBids();
    fetch_scoring_signals_query_kanon_tracker_.TaskCompleted(TaskStatus::ERROR);
  }
}

void SelectAdReactor::PopulateKAnonStatusForBids() {
  PS_VLOG(5, log_context_) << " " << __func__;
  for (auto& [bid, hashes_to_resolve] : bid_k_anon_hash_sets_) {
    bid_k_anon_status_[bid] = true;
    if (!enable_enforce_kanon_) {
      continue;
    }
    for (const auto& hash : hashes_to_resolve) {
      if (!queried_k_anon_hashes_.contains(hash)) {
        PS_VLOG(5, log_context_)
            << " " << __func__ << " Hash : " << absl::BytesToHexString(hash)
            << " is not in k-anon set";
        bid_k_anon_status_[bid] = false;
        break;
      } else {
        PS_VLOG(5, log_context_)
            << " " << __func__ << " Hash : " << absl::BytesToHexString(hash)
            << " is in k-anon set";
      }
    }
    if (bid_k_anon_status_[bid]) {
      PS_VLOG(5, log_context_)
          << " " << __func__ << " Following bid (key: " << bid
          << ") is k-anonymous:\n"
          << bid->DebugString()
          << ", with hashes: " << KAnonHashSetsToString(hashes_to_resolve);
    } else {
      PS_VLOG(5, log_context_)
          << " " << __func__ << " Following bid is not k-anonymous:\n"
          << bid->DebugString()
          << ", with hashes: " << KAnonHashSetsToString(hashes_to_resolve);
    }
  }
}

SelectAdReactor::BidKAnonHashSets SelectAdReactor::GetKAnonHashesForBids() {
  PS_VLOG(6, log_context_) << " " << __func__;
  const auto& buyer_report_win_js_urls =
      report_win_map_.buyer_report_win_js_urls;
  absl::flat_hash_map<const google::protobuf::Message*,
                      absl::flat_hash_set<std::string>>
      bid_k_anon_hashes;
  HashUtil k_anon_hash_util;
  for (const auto& [buyer_ig_owner, get_bids_raw_response] :
       shared_buyer_bids_map_) {
    if (get_bids_raw_response->bids().empty()) {
      continue;
    }

    auto reportin_win_it = buyer_report_win_js_urls.find(buyer_ig_owner);
    if (reportin_win_it == buyer_report_win_js_urls.end()) {
      PS_VLOG(5, log_context_)
          << "Unable to find buyer IG owner in win "
          << "reporting URLs, considering all related hashes as "
          << "non-k-anonymous for buyer: " << buyer_ig_owner;
      continue;
    }

    for (const auto& bid : get_bids_raw_response->bids()) {
      absl::flat_hash_set<std::string> k_anon_hashes_for_bid;
      k_anon_hashes_for_bid.insert(
          k_anon_hash_util.HashedKAnonKeyForAdRenderURL(
              buyer_ig_owner, reportin_win_it->second, bid.render()));
      LogIfError(metric_context_->AccumulateMetric<metric::kSfeKAnonHashCount>(
          1, kKAnonAdBidHash));

      // Reporting ID hashes.
      KAnonKeyReportingIDParam reporting_ids;
      if (bid.has_buyer_reporting_id()) {
        reporting_ids.buyer_reporting_id = bid.buyer_reporting_id();
      }
      if (bid.has_buyer_and_seller_reporting_id()) {
        reporting_ids.buyer_and_seller_reporting_id =
            bid.buyer_and_seller_reporting_id();
      }
      if (bid.has_selected_buyer_and_seller_reporting_id()) {
        reporting_ids.selected_buyer_and_seller_reporting_id =
            bid.selected_buyer_and_seller_reporting_id();
      }
      k_anon_hashes_for_bid.insert(
          k_anon_hash_util.HashedKAnonKeyForReportingID(
              buyer_ig_owner, bid.interest_group_name(),
              reportin_win_it->second, bid.render(), reporting_ids));
      LogIfError(metric_context_->AccumulateMetric<metric::kSfeKAnonHashCount>(
          1, kKAnonReportingIdHash));

      // Calculate hashes for ad component render URLs.
      for (const auto& ad_component_render : bid.ad_components()) {
        k_anon_hashes_for_bid.insert(
            k_anon_hash_util.HashedKAnonKeyForAdComponentRenderURL(
                ad_component_render));
      }
      if (!bid.ad_components().empty()) {
        LogIfError(
            metric_context_->AccumulateMetric<metric::kSfeKAnonHashCount>(
                bid.ad_components().size(), kKAnonAdComponentAdHash));
      }
      bid_k_anon_hashes[&bid] = std::move(k_anon_hashes_for_bid);
    }
  }
  return bid_k_anon_hashes;
}

}  // namespace privacy_sandbox::bidding_auction_servers
