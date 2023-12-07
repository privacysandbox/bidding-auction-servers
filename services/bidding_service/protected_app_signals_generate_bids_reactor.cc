// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/protected_app_signals_generate_bids_reactor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/constants.h"
#include "services/common/clients/http_kv_server/buyer/ads_retrieval_async_http_client.h"
#include "services/common/util/json_util.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_metadata.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using AdsRetrievalResult = absl::StatusOr<std::unique_ptr<AdRetrievalOutput>>;

inline constexpr char kClientIpKey[] = "x-bna-client-ip";
inline constexpr char kUserAgentKey[] = "x-user-agent";
inline constexpr char kAcceptLanguageKey[] = "x-accept-language";
inline constexpr int kNumMaxEgressBytes = 3;
inline constexpr int kEgressHighestBitMask = 0x800000;

inline constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
    kBuyerMetadataKeysMap = {{{kAcceptLanguageKey, kAcceptLanguageKey},
                              {kUserAgentKey, kUserAgentKey},
                              {kClientIpKey, kClientIpKey}}};

inline void PopulateArgInRomaRequest(
    absl::string_view arg, int index,
    std::vector<std::shared_ptr<std::string>>& request) {
  request[index] = std::make_shared<std::string>((arg.empty()) ? "\"\"" : arg);
}

// Checks that 24-th bit in egress feature vector is always clear (since we
// want to allow a maximum of 23-bits to egress).
inline bool IsEgressMSBClear(absl::string_view egress_features) {
  auto hex_string = absl::BytesToHexString(egress_features);
  PS_VLOG(5) << "Egress features as hex: " << hex_string;
  uint32_t out;
  if (auto success = absl::SimpleHexAtoi(hex_string, &out); !success) {
    PS_VLOG(1) << "Failed to convert hex egress features (bytes: "
               << egress_features << ", hex: " << hex_string << ") to integer";
    return false;
  }

  PS_VLOG(5) << "Egress features as int: " << out;
  return (out & kEgressHighestBitMask) == 0;
}

// Gets string information about a bid's well-formed-ness.
inline std::string GetBidDebugInfo(const ProtectedAppSignalsAdWithBid& bid) {
  return absl::StrCat(
      "Is non-zero bid: ", bid.bid() > 0.0f,
      ", Num egress bytes: ", bid.egress_features().size(),
      ", MSB in egress is clear: ", IsEgressMSBClear(bid.egress_features()),
      ", has debug report urls: ", bid.has_debug_report_urls());
}

// Validates that egress features don't exceed 23-bits in size.
inline bool IsValidEgress(absl::string_view egress_features) {
  return egress_features.empty() ||
         (egress_features.size() <= kNumMaxEgressBytes &&
          IsEgressMSBClear(egress_features));
}

}  // namespace

ProtectedAppSignalsGenerateBidsReactor::ProtectedAppSignalsGenerateBidsReactor(
    const grpc::CallbackServerContext* context,
    const CodeDispatchClient& dispatcher,
    const BiddingServiceRuntimeConfig& runtime_config,
    const GenerateProtectedAppSignalsBidsRequest* request,
    GenerateProtectedAppSignalsBidsResponse* response,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    AsyncClient<AdRetrievalInput, AdRetrievalOutput>*
        http_ad_retrieval_async_client)
    : BaseGenerateBidsReactor<GenerateProtectedAppSignalsBidsRequest,
                              GenerateProtectedAppSignalsBidsRequest::
                                  GenerateProtectedAppSignalsBidsRawRequest,
                              GenerateProtectedAppSignalsBidsResponse,
                              GenerateProtectedAppSignalsBidsResponse::
                                  GenerateProtectedAppSignalsBidsRawResponse>(
          dispatcher, runtime_config, request, response, key_fetcher_manager,
          crypto_client),
      http_ad_retrieval_async_client_(http_ad_retrieval_async_client),
      ad_bids_retrieval_timeout_ms_(runtime_config.ad_retrieval_timeout_ms),
      metadata_(GrpcMetadataToRequestMetadata(context->client_metadata(),
                                              kBuyerMetadataKeysMap)) {
  DCHECK(http_ad_retrieval_async_client_) << "Missing: Ads Async HTTP client";
}

absl::Status ProtectedAppSignalsGenerateBidsReactor::ValidateRomaResponse(
    const std::vector<absl::StatusOr<DispatchResponse>>& result) {
  if (result.size() != 1) {
    return absl::InvalidArgumentError(kUnexpectedNumberOfRomaResponses);
  }

  const auto& response = result[0];
  if (!response.ok()) {
    return response.status();
  }

  return absl::OkStatus();
}

std::unique_ptr<AdRetrievalInput>
ProtectedAppSignalsGenerateBidsReactor::CreateAdsRetrievalRequest(
    const std::string& prepare_data_for_ads_retrieval_response) {
  return std::make_unique<AdRetrievalInput>(AdRetrievalInput{
      .retrieval_data = prepare_data_for_ads_retrieval_response,
      .contextual_signals = raw_request_.buyer_signals(),
      .device_metadata = {.client_ip = metadata_[kClientIpKey],
                          .user_agent = metadata_[kUserAgentKey],
                          .accept_language = metadata_[kAcceptLanguageKey]},
  });
}

void ProtectedAppSignalsGenerateBidsReactor::FetchAds(
    const std::string& prepare_data_for_ads_retrieval_response) {
  PS_VLOG(8, log_context_) << __func__;
  auto status = http_ad_retrieval_async_client_->Execute(
      CreateAdsRetrievalRequest(prepare_data_for_ads_retrieval_response), {},
      [this, prepare_data_for_ads_retrieval_response](
          AdsRetrievalResult ad_retrieval_kv_output) {
        if (!ad_retrieval_kv_output.ok()) {
          PS_VLOG(2, log_context_) << "Ad retrieval request failed: "
                                   << ad_retrieval_kv_output.status();
          EncryptResponseAndFinish(grpc::Status(
              grpc::INTERNAL, ad_retrieval_kv_output.status().ToString()));
          return;
        }

        OnFetchAdsDone(*std::move(ad_retrieval_kv_output),
                       prepare_data_for_ads_retrieval_response);
      },
      absl::Milliseconds(ad_bids_retrieval_timeout_ms_));

  if (!status.ok()) {
    PS_VLOG(2, log_context_)
        << "Failed to execute ad retrieval request: " << status;
    EncryptResponseAndFinish(grpc::Status(grpc::INTERNAL, status.ToString()));
  }
}

DispatchRequest
ProtectedAppSignalsGenerateBidsReactor::CreateGenerateBidsRequest(
    std::unique_ptr<AdRetrievalOutput> result,
    absl::string_view prepare_data_for_ads_retrieval_response) {
  std::vector<std::shared_ptr<std::string>> input(
      kNumGenerateBidsUdfArgs, std::make_shared<std::string>());
  PopulateArgInRomaRequest(result->ads, ArgIndex(GenerateBidsUdfArgs::kAds),
                           input);
  PopulateArgInRomaRequest(raw_request_.auction_signals(),
                           ArgIndex(GenerateBidsUdfArgs::kAuctionSignals),
                           input);
  PopulateArgInRomaRequest(raw_request_.buyer_signals(),
                           ArgIndex(GenerateBidsUdfArgs::kBuyerSignals), input);
  PopulateArgInRomaRequest(
      prepare_data_for_ads_retrieval_response,
      ArgIndex(GenerateBidsUdfArgs::kPreProcessedDataForRetrieval), input);
  PopulateArgInRomaRequest(
      GetFeatureFlagJson(enable_adtech_code_logging_,
                         enable_buyer_debug_url_generation_ &&
                             raw_request_.enable_debug_reporting()),
      ArgIndex(GenerateBidsUdfArgs::kFeatureFlags), input);
  DispatchRequest request = {
      .id = raw_request_.log_context().generation_id(),
      .version_num = kProtectedAppSignalsGenerateBidBlobVersion,
      .handler_name = kDispatchHandlerFunctionNameWithCodeWrapper,
      .input = std::move(input),
  };
  request.tags[kTimeoutMs] = roma_timeout_ms_;
  return request;
}

absl::StatusOr<ProtectedAppSignalsAdWithBid>
ProtectedAppSignalsGenerateBidsReactor::
    ParseProtectedSignalsGenerateBidsResponse(const std::string& response) {
  PS_VLOG(8, log_context_) << __func__;
  PS_ASSIGN_OR_RETURN(auto generate_bid_response,
                      ParseAndGetResponseJson(enable_adtech_code_logging_,
                                              response, log_context_),
                      _ << "Failed to parse ProtectedAppSignalsAdWithBid JSON "
                           "response from Roma");
  ProtectedAppSignalsAdWithBid bid;
  PS_RETURN_IF_ERROR(
      google::protobuf::util::JsonStringToMessage(generate_bid_response, &bid));
  return bid;
}

void ProtectedAppSignalsGenerateBidsReactor::OnFetchAdsDone(
    std::unique_ptr<AdRetrievalOutput> result,
    const std::string& prepare_data_for_ads_retrieval_response) {
  PS_VLOG(8, log_context_) << __func__
                           << "Ads data returned by the ad retrieval service: "
                           << result->ads;
  if (result->ads.empty()) {
    PS_VLOG(4, log_context_) << "No ads data returned by the ad retrieval "
                                "service, finishing RPC";
    EncryptResponseAndFinish(grpc::Status::OK);
    return;
  }

  dispatch_requests_.emplace_back(CreateGenerateBidsRequest(
      std::move(result), prepare_data_for_ads_retrieval_response));
  ExecuteRomaRequests<ProtectedAppSignalsAdWithBid>(
      dispatch_requests_, kDispatchHandlerFunctionNameWithCodeWrapper,
      [this](const std::string& response) {
        return ParseProtectedSignalsGenerateBidsResponse(response);
      },
      [this](const ProtectedAppSignalsAdWithBid& bid) {
        if (!IsValidBid(bid)) {
          PS_VLOG(2, log_context_) << "Skipping protected app signals bid ("
                                   << GetBidDebugInfo(bid) << ")";
        } else {
          PS_VLOG(3, log_context_)
              << "Successful non-zero protected app signals bid received";
          auto* added_bid = raw_response_.add_bids();
          *added_bid = bid;
          added_bid->clear_egress_features();
        }
        EncryptResponseAndFinish(grpc::Status::OK);
      });
}

DispatchRequest ProtectedAppSignalsGenerateBidsReactor::
    CreatePrepareDataForAdsRetrievalRequest() {
  PS_VLOG(8, log_context_) << __func__;
  std::vector<std::shared_ptr<std::string>> input(
      kNumPrepareDataForRetrievalUdfArgs, std::make_shared<std::string>());
  PopulateArgInRomaRequest(
      absl::StrCat(
          "\"",
          absl::BytesToHexString(
              raw_request_.protected_app_signals().app_install_signals()),
          "\""),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kProtectedAppSignals), input);
  PopulateArgInRomaRequest(
      absl::StrCat(raw_request_.protected_app_signals().encoding_version()),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kProtectedAppSignalsVersion),
      input);
  PopulateArgInRomaRequest(
      raw_request_.auction_signals(),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kAuctionSignals), input);
  PopulateArgInRomaRequest(
      raw_request_.buyer_signals(),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kBuyerSignals), input);
  PopulateArgInRomaRequest(
      GetFeatureFlagJson(enable_adtech_code_logging_),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kFeatureFlags), input);
  DispatchRequest request = {
      .id = raw_request_.log_context().generation_id(),
      .version_num = kPrepareDataForAdRetrievalBlobVersion,
      .handler_name = kPrepareDataForAdRetrievalEntryFunctionName,
      .input = std::move(input),
  };
  if (log::PS_VLOG_IS_ON(3)) {
    for (const auto& i : request.input) {
      PS_VLOG(3, log_context_)
          << "Roma request input to prepared data for ads retrieval: " << *i;
    }
  }
  request.tags[kTimeoutMs] = roma_timeout_ms_;
  return request;
}

void ProtectedAppSignalsGenerateBidsReactor::GetPreparedDataForAdsRetrieval() {
  PS_VLOG(8, log_context_) << __func__;
  embeddings_requests_.emplace_back(CreatePrepareDataForAdsRetrievalRequest());
  ExecuteRomaRequests<std::string>(
      embeddings_requests_, kPrepareDataForAdRetrievalHandler,
      [](const std::string& response) -> absl::StatusOr<std::string> {
        PS_ASSIGN_OR_RETURN(rapidjson::Document document,
                            ParseJsonString(response));
        return SerializeJsonDoc(document["response"]);
      },
      [this](const std::string& parsed_response) {
        FetchAds(parsed_response);
      });
}

void ProtectedAppSignalsGenerateBidsReactor::Execute() {
  PS_VLOG(8) << __func__;
  PS_VLOG(2, log_context_) << "GenerateBidsRequest:\n"
                           << request_->DebugString();
  PS_VLOG(1, log_context_) << "GenerateBidsRawRequest:\n"
                           << raw_request_.DebugString();

  // Trigger the request processing workflow to:
  // 1. Fetch protected embeddings for retrieval
  // 2. Fetch top-k ads and metadata using the embeddings retrieved in 1.
  // 3. Run the `generateBid` UDF for Protected App Signals and return the
  //    response back to BFE.
  GetPreparedDataForAdsRetrieval();
}

void ProtectedAppSignalsGenerateBidsReactor::OnDone() { delete this; }

void ProtectedAppSignalsGenerateBidsReactor::OnCancel() {}

void ProtectedAppSignalsGenerateBidsReactor::EncryptResponseAndFinish(
    grpc::Status status) {
  PS_VLOG(8, log_context_) << __func__;
  if (!EncryptResponse()) {
    PS_VLOG(1, log_context_)
        << "Failed to encrypt the generate app signals bids response.";
    status = grpc::Status(grpc::INTERNAL, kInternalServerError);
  }
  Finish(status);
}

}  // namespace privacy_sandbox::bidding_auction_servers
