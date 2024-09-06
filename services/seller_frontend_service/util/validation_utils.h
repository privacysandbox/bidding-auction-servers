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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_VALIDATION_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_VALIDATION_UTILS_H_

#include <string>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/error_categories.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kValidCurrencyCodePattern[] = "^[A-Z]{3}$";

std::regex GetValidCurrencyCodeRegex();

// Validates a single Auction Result and adds errors to error accumulator.
// Returns false if AuctionResult is invalid.
bool ValidateComponentAuctionResult(
    const AuctionResult& auction_result,
    absl::string_view request_generation_id, absl::string_view seller_domain,
    ErrorAccumulator& error_accumulator,
    const ::google::protobuf::Map<
        std::string, SelectAdRequest::AuctionConfig::PerComponentSellerConfig>&
        per_conponent_seller_configs = {});

// Validates an encrypted SelectAdRequest and adds all errors to error
// accumulator.
// Returns false if plaintext fields in SelectAdRequest are invalid.
bool ValidateEncryptedSelectAdRequest(const SelectAdRequest& request,
                                      AuctionScope auction_scope,
                                      absl::string_view seller_domain,
                                      ErrorAccumulator& error_accumulator);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_PROTO_MAPPING_UTIL_H_
