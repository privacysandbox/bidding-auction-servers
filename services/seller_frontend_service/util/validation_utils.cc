//  Copyright 2024 Google LLC
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

#include "services/seller_frontend_service/util/validation_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

bool IsAuctionResultBidCurrencyAsExpected(
    absl::string_view expected_component_seller_currency,
    const AuctionResult& auction_result) {
  if (expected_component_seller_currency.empty()) {
    return true;
  }
  if (auction_result.bid_currency().empty()) {
    return true;
  }
  return expected_component_seller_currency == auction_result.bid_currency();
}

}  // namespace

std::regex GetValidCurrencyCodeRegex() {
  static const std::regex kValidCurrencyCodeRegexVal(kValidCurrencyCodePattern);
  return kValidCurrencyCodeRegexVal;
}

bool ValidateEncryptedSelectAdRequest(const SelectAdRequest& request,
                                      AuctionScope auction_scope,
                                      absl::string_view seller_domain,
                                      ErrorAccumulator& error_accumulator) {
  bool valid = true;
  auto report_error_lambda = [&error_accumulator, &valid](
                                 bool has_error, absl::string_view error_msg) {
    if (has_error) {
      valid = false;
      error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                                    error_msg, ErrorCode::CLIENT_SIDE);
    }
  };
  report_error_lambda(request.protected_auction_ciphertext().empty() &&
                          request.protected_audience_ciphertext().empty(),
                      kEmptyProtectedAuctionCiphertextError);
  const SelectAdRequest::AuctionConfig& auction_config =
      request.auction_config();

  report_error_lambda(auction_config.seller_signals().empty(),
                      kEmptySellerSignals);
  report_error_lambda(auction_config.auction_signals().empty(),
                      kEmptyAuctionSignals);
  report_error_lambda(auction_config.seller().empty(), kEmptySeller);
  report_error_lambda(seller_domain != auction_config.seller(),
                      kWrongSellerDomain);
  report_error_lambda(request.client_type() == CLIENT_TYPE_UNKNOWN,
                      kUnsupportedClientType);

  if (auction_scope == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    report_error_lambda(request.component_auction_results_size() == 0,
                        kNoComponentAuctionResults);
    int valid_results = 0;
    for (const auto& auction_result : request.component_auction_results()) {
      if (auction_result.auction_result_ciphertext().empty() ||
          auction_result.key_id().empty()) {
        continue;
      }
      ++valid_results;
    }
    report_error_lambda(
        valid_results == 0 && request.component_auction_results_size() != 0,
        kEmptyComponentAuctionResults);

    for (const auto& [seller_identifier, per_component_seller_config] :
         auction_config.per_component_seller_config()) {
      report_error_lambda(seller_identifier.empty(),
                          kEmptySellerInPerComponentSellerConfig);
      if (!per_component_seller_config.expected_currency().empty()) {
        report_error_lambda(
            !std::regex_match(per_component_seller_config.expected_currency(),
                              GetValidCurrencyCodeRegex()),
            kInvalidExpectedComponentSellerCurrency);
      }
    }
  }
  return valid;
}

bool ValidateComponentAuctionResult(
    const AuctionResult& auction_result,
    absl::string_view request_generation_id, absl::string_view seller_domain,
    ErrorAccumulator& error_accumulator,
    const ::google::protobuf::Map<
        std::string, SelectAdRequest::AuctionConfig::PerComponentSellerConfig>&
        per_component_seller_configs) {
  // If chaff result or erroneous request, don't do any more validations and
  // pass as is to auction server to make sure auction server is called.
  if (auction_result.is_chaff() || !auction_result.error().message().empty()) {
    return true;
  }

  bool valid = true;
  auto report_error_lambda = [&error_accumulator, &valid](
                                 bool has_error, absl::string_view error_msg) {
    if (has_error) {
      valid = false;
      error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                                    error_msg, ErrorCode::CLIENT_SIDE);
    }
  };

  report_error_lambda(
      auction_result.auction_params().ciphertext_generation_id() !=
          request_generation_id,
      absl::StrFormat(kErrorInAuctionResult,
                      kMismatchedGenerationIdInAuctionResultError));
  report_error_lambda(
      auction_result.top_level_seller() != seller_domain,
      absl::StrFormat(kErrorInAuctionResult,
                      kMismatchedTopLevelSellerInAuctionResultError));

  report_error_lambda(
      auction_result.auction_params().component_seller().empty(),
      absl::StrFormat(kErrorInAuctionResult,
                      kEmptyComponentSellerInAuctionResultError));

  // Retrieve per-component-seller configuration values.
  absl::string_view expected_component_seller_currency;
  // First check that the per seller config map is non-empty, and that there
  // is a component seller identifier to use for the lookup.
  if (per_component_seller_configs.size() > 0) {
    // Try to find the configuration information for this particular component
    // seller.
    auto per_component_seller_config_itr = per_component_seller_configs.find(
        auction_result.auction_params().component_seller());
    // The PerComponentSellerConfig is optional.
    if (per_component_seller_config_itr != per_component_seller_configs.end()) {
      const auto& component_seller_config =
          per_component_seller_config_itr->second;
      // Check if a particular currency is expected.
      if (!component_seller_config.expected_currency().empty()) {
        expected_component_seller_currency =
            component_seller_config.expected_currency();
      }
    }
  }

  report_error_lambda(
      ((auction_result.ad_type() != AdType::AD_TYPE_PROTECTED_AUDIENCE_AD) &&
       (auction_result.ad_type() != AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD)),
      absl::StrFormat(kErrorInAuctionResult,
                      kUnsupportedAdTypeInAuctionResultError));

  report_error_lambda(
      !auction_result.win_reporting_urls()
           .top_level_seller_reporting_urls()
           .reporting_url()
           .empty(),
      absl::StrFormat(kErrorInAuctionResult,
                      kTopLevelWinReportingUrlsInAuctionResultError));
  report_error_lambda(!IsAuctionResultBidCurrencyAsExpected(
                          expected_component_seller_currency, auction_result),
                      absl::StrFormat(kErrorInAuctionResult,
                                      kMismatchedCurrencyInAuctionResultError));
  return valid;
}

}  // namespace privacy_sandbox::bidding_auction_servers
