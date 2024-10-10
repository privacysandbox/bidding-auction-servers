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

#include "services/seller_frontend_service/util/proto_mapping_util.h"

#include "services/seller_frontend_service/util/framing_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

void SetReportingUrls(const WinReportingUrls& win_reporting_urls,
                      AuctionResult& auction_result) {
  auto* mutable_win_reporting_urls =
      auction_result.mutable_win_reporting_urls();
  mutable_win_reporting_urls->mutable_buyer_reporting_urls()->set_reporting_url(
      win_reporting_urls.buyer_reporting_urls().reporting_url());
  *mutable_win_reporting_urls->mutable_buyer_reporting_urls()
       ->mutable_interaction_reporting_urls() =
      win_reporting_urls.buyer_reporting_urls().interaction_reporting_urls();
  mutable_win_reporting_urls->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(
          win_reporting_urls.top_level_seller_reporting_urls().reporting_url());
  *mutable_win_reporting_urls->mutable_top_level_seller_reporting_urls()
       ->mutable_interaction_reporting_urls() =
      win_reporting_urls.top_level_seller_reporting_urls()
          .interaction_reporting_urls();
  mutable_win_reporting_urls->mutable_component_seller_reporting_urls()
      ->set_reporting_url(
          win_reporting_urls.component_seller_reporting_urls().reporting_url());

  *mutable_win_reporting_urls->mutable_component_seller_reporting_urls()
       ->mutable_interaction_reporting_urls() =
      win_reporting_urls.component_seller_reporting_urls()
          .interaction_reporting_urls();
}

AuctionResult MapAdScoreToAuctionResult(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const std::optional<AuctionResult::Error>& error) {
  AuctionResult auction_result;
  if (error.has_value()) {
    *auction_result.mutable_error() = *error;
  } else if (high_score.has_value()) {
    auction_result.set_is_chaff(false);
    if (high_score->bid() > 0) {
      auction_result.set_bid(high_score->bid());
    }
    auction_result.set_score(high_score->desirability());
    auction_result.set_interest_group_name(high_score->interest_group_name());
    auction_result.set_interest_group_owner(high_score->interest_group_owner());
    auction_result.set_interest_group_origin(
        high_score->interest_group_origin());
    auction_result.set_ad_render_url(high_score->render());
    auction_result.mutable_win_reporting_urls()
        ->mutable_buyer_reporting_urls()
        ->set_reporting_url(high_score->win_reporting_urls()
                                .buyer_reporting_urls()
                                .reporting_url());
    for (auto& [event, url] : high_score->win_reporting_urls()
                                  .buyer_reporting_urls()
                                  .interaction_reporting_urls()) {
      auction_result.mutable_win_reporting_urls()
          ->mutable_buyer_reporting_urls()
          ->mutable_interaction_reporting_urls()
          ->try_emplace(event, url);
    }
    SetReportingUrls(high_score->win_reporting_urls(), auction_result);
    if (high_score->top_level_contributions().size() > 0) {
      *auction_result.mutable_top_level_contributions() =
          high_score->top_level_contributions();
    }
    *auction_result.mutable_ad_component_render_urls() =
        high_score->component_renders();
    auction_result.set_ad_type(high_score->ad_type());
  } else {
    auction_result.set_is_chaff(true);
  }
  return auction_result;
}

absl::StatusOr<std::string> PackageAuctionResultCiphertext(
    absl::string_view serialized_auction_result,
    OhttpHpkeDecryptedMessage& decrypted_request) {
  // Gzip compress.
  PS_ASSIGN_OR_RETURN(std::string compressed_data,
                      GzipCompress(serialized_auction_result));

  // Frame(set bits) and pad.
  PS_ASSIGN_OR_RETURN(
      std::string encoded_plaintext,
      server_common::EncodeResponsePayload(
          server_common::CompressionType::kGzip, compressed_data,
          GetEncodedDataSize(compressed_data.size())));

  // Encapsulate and encrypt with corresponding private key.
  return server_common::EncryptAndEncapsulateResponse(
      std::move(encoded_plaintext), decrypted_request.private_key,
      decrypted_request.context, decrypted_request.request_label);
}

absl::StatusOr<std::string> PackageAuctionResultForWeb(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const std::optional<IgsWithBidsMap>& maybe_bidding_group_map,
    const UpdateGroupMap& update_group_map,
    const std::optional<AuctionResult::Error>& error,
    OhttpHpkeDecryptedMessage& decrypted_request,
    RequestLogContext& log_context) {
  std::string error_msg;
  absl::Notification wait_for_error_callback;
  auto error_handler = [&wait_for_error_callback,
                        &error_msg](const grpc::Status& status) {
    error_msg = status.error_message();
    wait_for_error_callback.Notify();
  };
  PS_VLOG(kNoisyInfo, log_context)
      << "IG bids map size:"
      << (maybe_bidding_group_map.has_value()
              ? absl::StrCat("found: ", maybe_bidding_group_map->size())
              : "Not found");
  absl::StatusOr<std::string> serialized_data =
      Encode(high_score,
             maybe_bidding_group_map.has_value() ? *maybe_bidding_group_map
                                                 : IgsWithBidsMap(),
             update_group_map, error, error_handler);
  if (!serialized_data.ok()) {
    wait_for_error_callback.WaitForNotification();
    return absl::Status(serialized_data.status().code(), error_msg);
  } else {
    PS_VLOG(kPlain, log_context)
        << "AuctionResult: " << [&](absl::string_view encoded_data) {
             auto result = CborDecodeAuctionResultToProto(encoded_data);
             if (result.ok()) {
               log_context.SetEventMessageField(*result);
               return std::string("exported in EventMessage");
             } else {
               return result.status().ToString();
             }
           }(*serialized_data);
  }
  absl::string_view data_to_compress =
      absl::string_view(reinterpret_cast<char*>((*serialized_data).data()),
                        (*serialized_data).size());
  return PackageAuctionResultCiphertext(data_to_compress, decrypted_request);
}

absl::StatusOr<std::string> PackageAuctionResultForApp(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const std::optional<AuctionResult::Error>& error,
    OhttpHpkeDecryptedMessage& decrypted_request,
    RequestLogContext& log_context) {
  // Map to AuctionResult proto and serialized to bytes array.

  AuctionResult result = MapAdScoreToAuctionResult(high_score, error);
  PS_VLOG(kPlain, log_context) << "AuctionResult exported in EventMessage";
  log_context.SetEventMessageField(result);
  return PackageAuctionResultCiphertext(result.SerializeAsString(),
                                        decrypted_request);
}

absl::StatusOr<std::string> PackageAuctionResultForInvalid(
    ClientType client_type) {
  return absl::Status(absl::StatusCode::kInvalidArgument,
                      absl::StrCat(kUnsupportedClientType, " (",
                                   ClientType_Name(client_type), ")"));
}

}  // namespace

AuctionResult AdScoreToAuctionResult(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    std::optional<IgsWithBidsMap> bidding_groups, UpdateGroupMap& update_groups,
    const std::optional<AuctionResult::Error>& error,
    AuctionScope auction_scope, absl::string_view seller,
    const std::variant<ProtectedAudienceInput, ProtectedAuctionInput>&
        protected_auction_input,
    absl::string_view top_level_seller) {
  AuctionResult auction_result = MapAdScoreToAuctionResult(high_score, error);
  if (high_score.has_value() &&
      auction_scope ==
          AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
    absl::string_view generation_id;
    std::visit(
        [&generation_id](const auto& protected_auction_input) {
          generation_id = protected_auction_input.generation_id();
        },
        protected_auction_input);
    auction_result.mutable_auction_params()->set_ciphertext_generation_id(
        generation_id);
    auction_result.mutable_auction_params()->set_component_seller(seller);
    auction_result.set_top_level_seller(top_level_seller);
    if (bidding_groups.has_value()) {
      *auction_result.mutable_bidding_groups() = (*std::move(bidding_groups));
    }

    for (auto& [ig_owner, updates] : update_groups) {
      (*auction_result.mutable_update_groups())[ig_owner] = std::move(updates);
    }
  }
  return auction_result;
}

std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
CreateTopLevelScoreAdsRawRequest(
    const SelectAdRequest::AuctionConfig& auction_config,
    std::variant<ProtectedAudienceInput, ProtectedAuctionInput>&
        protected_auction_input,
    std::vector<AuctionResult>& component_auction_results) {
  auto raw_request = std::make_unique<ScoreAdsRequest::ScoreAdsRawRequest>();
  *raw_request->mutable_auction_signals() = auction_config.auction_signals();
  *raw_request->mutable_seller_signals() = auction_config.seller_signals();
  *raw_request->mutable_seller() = auction_config.seller();
  *raw_request->mutable_seller_currency() = auction_config.seller_currency();
  // Move Component Auctions Results.
  for (auto component_auction_itr =
           std::make_move_iterator(component_auction_results.begin());
       component_auction_itr !=
       std::make_move_iterator(component_auction_results.end());
       ++component_auction_itr) {
    if (!component_auction_itr->is_chaff()) {
      raw_request->mutable_component_auction_results()->Add(
          std::move(*component_auction_itr));
    }
  }
  std::visit(
      [&raw_request, &auction_config](const auto& protected_auction_input) {
        raw_request->set_publisher_hostname(
            protected_auction_input.publisher_name());
        raw_request->set_enable_debug_reporting(
            protected_auction_input.enable_debug_reporting());
        auto* log_context = raw_request->mutable_log_context();
        log_context->set_generation_id(protected_auction_input.generation_id());
        log_context->set_adtech_debug_id(auction_config.seller_debug_id());
        if (protected_auction_input.has_consented_debug_config()) {
          *raw_request->mutable_consented_debug_config() =
              protected_auction_input.consented_debug_config();
        }
      },
      protected_auction_input);
  return raw_request;
}

absl::StatusOr<std::string> CreateWinningAuctionResultCiphertext(
    const ScoreAdsResponse::AdScore& ad_score,
    const std::optional<IgsWithBidsMap>& bidding_group_map,
    const UpdateGroupMap& update_group_map, ClientType client_type,
    OhttpHpkeDecryptedMessage& decrypted_request,
    RequestLogContext& log_context) {
  absl::StatusOr<std::string> auction_result_ciphertext;
  switch (client_type) {
    case CLIENT_TYPE_ANDROID:
      return PackageAuctionResultForApp(ad_score, /*error =*/std::nullopt,
                                        decrypted_request, log_context);
    case CLIENT_TYPE_BROWSER:
      return PackageAuctionResultForWeb(
          ad_score, bidding_group_map, update_group_map,
          /*error =*/std::nullopt, decrypted_request, log_context);
    default:
      return PackageAuctionResultForInvalid(client_type);
  }
}

absl::StatusOr<std::string> CreateErrorAuctionResultCiphertext(
    const AuctionResult::Error& auction_error, ClientType client_type,
    OhttpHpkeDecryptedMessage& decrypted_request,
    RequestLogContext& log_context) {
  switch (client_type) {
    case CLIENT_TYPE_ANDROID:
      return PackageAuctionResultForApp(
          /*high_score=*/std::nullopt, auction_error, decrypted_request,
          log_context);
    case CLIENT_TYPE_BROWSER:
      return PackageAuctionResultForWeb(
          /*high_score=*/std::nullopt, /*maybe_bidding_group_map=*/std::nullopt,
          /*update_group_map=*/{}, auction_error, decrypted_request,
          log_context);
    default:
      return PackageAuctionResultForInvalid(client_type);
  }
}

absl::StatusOr<std::string> CreateChaffAuctionResultCiphertext(
    ClientType client_type, OhttpHpkeDecryptedMessage& decrypted_request,
    RequestLogContext& log_context) {
  switch (client_type) {
    case CLIENT_TYPE_ANDROID:
      return PackageAuctionResultForApp(
          /*high_score=*/std::nullopt, /*error =*/std::nullopt,
          decrypted_request, log_context);
    case CLIENT_TYPE_BROWSER:
      return PackageAuctionResultForWeb(
          /*high_score=*/std::nullopt, /*maybe_bidding_group_map=*/std::nullopt,
          /*update_group_map=*/{},
          /*error =*/std::nullopt, decrypted_request, log_context);
    default:
      return PackageAuctionResultForInvalid(client_type);
  }
}

IgsWithBidsMap GetBuyerIgsWithBidsMap(
    std::vector<IgsWithBidsMap>& component_auction_bidding_groups) {
  absl::flat_hash_map<absl::string_view, absl::flat_hash_set<int>>
      colliding_buyer_sets;
  IgsWithBidsMap buyer_to_ig_idx_map;
  for (IgsWithBidsMap& component_auction_ig_group :
       component_auction_bidding_groups) {
    PS_VLOG(kNoisyInfo) << "Size of input buyer bids map "
                        << component_auction_ig_group.size();
    for (auto& [buyer, ig_idx] : component_auction_ig_group) {
      // Check if the key already exists in the output map
      const auto& it = buyer_to_ig_idx_map.find(buyer);
      if (it == buyer_to_ig_idx_map.end()) {
        // Insert the entire entry
        buyer_to_ig_idx_map.insert({buyer, std::move(ig_idx)});
        PS_VLOG(kNoisyInfo) << "Inserted for " << buyer;
        continue;
      } else if (auto buyer_ig_set = colliding_buyer_sets.find(buyer);
                 buyer_ig_set == colliding_buyer_sets.end()) {
        // Not in Colliding buyer set but present in buyer_to_ig_idx_map.
        // Add values from previous CARs.
        absl::flat_hash_set<int> ig_set(it->second.index().begin(),
                                        it->second.index().end());
        PS_VLOG(kNoisyInfo)
            << "Inserted in colliding for" << buyer << ig_set.size();
        colliding_buyer_sets.insert({buyer, std::move(ig_set)});
      }
      // Already in colliding buyer set. Add all values from current CAR.
      PS_VLOG(kNoisyInfo) << "Inserted in colliding for" << buyer
                          << ig_idx.index().size();
      colliding_buyer_sets.at(buyer).insert(ig_idx.index().begin(),
                                            ig_idx.index().end());
    }
  }

  // Replace colliding ig index sets in proto.
  for (auto& [buyer, ig_idx_set] : colliding_buyer_sets) {
    buyer_to_ig_idx_map.at(buyer).mutable_index()->Assign(ig_idx_set.begin(),
                                                          ig_idx_set.end());
  }
  PS_VLOG(kNoisyInfo) << "Size of output buyer bids map "
                      << buyer_to_ig_idx_map.size();
  return buyer_to_ig_idx_map;
}

absl::StatusOr<AuctionResult> UnpackageServerAuctionComponentResult(
    const SelectAdRequest::ComponentAuctionResult& component_auction_result,
    CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager) {
  PS_ASSIGN_OR_RETURN(
      std::string encoded,
      HpkeDecrypt(component_auction_result.auction_result_ciphertext(),
                  component_auction_result.key_id(), crypto_client,
                  key_fetcher_manager));
  AuctionResult proto;
  PS_ASSIGN_OR_RETURN((server_common::DecodedRequest decoded_request),
                      server_common::DecodeRequestPayload(encoded));

  PS_ASSIGN_OR_RETURN(std::string payload,
                      GzipDecompress(decoded_request.compressed_data));
  PS_VLOG(kNoisyInfo) << "Parsing Auction Result: " << payload;
  if (!proto.ParseFromArray(payload.data(), payload.size())) {
    return absl::Status(absl::StatusCode::kInvalidArgument, kBadBinaryProto);
  }
  return proto;
}

std::vector<AuctionResult> DecryptAndValidateComponentAuctionResults(
    const SelectAdRequest* request, absl::string_view seller_domain,
    absl::string_view request_generation_id,
    CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    ErrorAccumulator& error_accumulator, RequestLogContext& log_context) {
  std::vector<AuctionResult> component_auction_results;
  // Keep track of encountered sellers.
  absl::flat_hash_set<std::string> component_sellers;
  component_auction_results.reserve(request->component_auction_results_size());
  for (const auto& enc_auction_result : request->component_auction_results()) {
    auto auction_result = UnpackageServerAuctionComponentResult(
        enc_auction_result, crypto_client, key_fetcher_manager);
    if (!auction_result.ok()) {
      std::string error_msg =
          absl::StrFormat(kErrorDecryptingAuctionResultError,
                          auction_result.status().message());
      PS_VLOG(kNoisyWarn, log_context) << error_msg;
      // Report error. This will be later returned to client in case
      // none of the auction results could be decoded.
      error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                                    std::move(error_msg),
                                    ErrorCode::CLIENT_SIDE);
      continue;
    }
    PS_VLOG(kSuccess, log_context)
        << "Successfully decrypted auction result ciphertext for: "
        << auction_result->auction_params().component_seller();
    PS_VLOG(kNoisyInfo, log_context)
        << "Bidding group size: " << auction_result->bidding_groups().size();

    // Add errors from AuctionResult to error_accumulator in
    // ValidateComponentAuctionResult.
    if (!ValidateComponentAuctionResult(
            *auction_result, request_generation_id, seller_domain,
            error_accumulator,
            request->auction_config().per_component_seller_config())) {
      PS_LOG(ERROR, log_context)
          << "Auction result skipped with failed validation for: "
          << auction_result->auction_params().component_seller();
      continue;
    }
    auto [itr, inserted] = component_sellers.insert(
        auction_result->auction_params().component_seller());
    // Duplicate seller name.
    if (!inserted) {
      std::string error_msg = absl::StrCat(
          kMultipleComponentAuctionResultsError,
          auction_result->auction_params().component_seller(), ".");
      // Report error. This will be later returned to the ad server.
      // Marked as CLIENT_SIDE since the error originates from the
      // API client.
      error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE,
                                    std::move(error_msg),
                                    ErrorCode::CLIENT_SIDE);
      // Return empty vector to abort auction.
      return std::vector<AuctionResult>();
    }
    PS_VLOG(kSuccess, log_context)
        << "Successfully validated auction result for: "
        << auction_result->auction_params().component_seller();
    component_auction_results.push_back(*std::move(auction_result));
  }
  return component_auction_results;
}

ProtectedAudienceInput DecryptProtectedAudienceInput(
    absl::string_view encapsulated_req,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    ClientType client_type, ErrorAccumulator& error_accumulator) {
  ProtectedAudienceInput protected_auction_input;
  switch (client_type) {
    case CLIENT_TYPE_ANDROID:
      protected_auction_input =
          AppProtectedAuctionInputDecodeHelper<ProtectedAudienceInput>(
              encapsulated_req, error_accumulator);
      break;
    case CLIENT_TYPE_BROWSER:
      protected_auction_input =
          WebProtectedAuctionInputDecodeHelper<ProtectedAudienceInput>(
              encapsulated_req, true, error_accumulator);
      break;
    default:
      break;
  }
  PS_VLOG(kSuccess)
      << "Successfully decrypted protected audience input ciphertext";
  return protected_auction_input;
}

ProtectedAuctionInput DecryptProtectedAuctionInput(
    absl::string_view encapsulated_req,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    ClientType client_type, ErrorAccumulator& error_accumulator) {
  ProtectedAuctionInput protected_auction_input;
  switch (client_type) {
    case CLIENT_TYPE_ANDROID:
      protected_auction_input =
          AppProtectedAuctionInputDecodeHelper<ProtectedAuctionInput>(
              encapsulated_req, error_accumulator);
      break;
    case CLIENT_TYPE_BROWSER:
      protected_auction_input =
          WebProtectedAuctionInputDecodeHelper<ProtectedAuctionInput>(
              encapsulated_req, true, error_accumulator);
      break;
    default:
      break;
  }
  PS_VLOG(kSuccess)
      << "Successfully decrypted protected audience input ciphertext";
  return protected_auction_input;
}

IgsWithBidsMap GetBiddingGroups(
    const BuyerBidsResponseMap& shared_buyer_bids_map,
    const absl::flat_hash_map<absl::string_view, BuyerInput>& buyer_inputs) {
  IgsWithBidsMap bidding_groups;
  for (const auto& [buyer, ad_with_bids] : shared_buyer_bids_map) {
    // Mapping from buyer to interest groups that are associated with non-zero
    // bids.
    absl::flat_hash_set<absl::string_view> buyer_interest_groups;
    for (const auto& ad_with_bid : ad_with_bids->bids()) {
      if (ad_with_bid.bid() > 0) {
        buyer_interest_groups.insert(ad_with_bid.interest_group_name());
      }
    }
    const auto& buyer_input = buyer_inputs.at(buyer);
    AuctionResult::InterestGroupIndex ar_interest_group_index;
    int ig_index = 0;
    for (const auto& interest_group : buyer_input.interest_groups()) {
      // If the interest group name is one of the groups returned by the bidding
      // service then record its index.
      if (buyer_interest_groups.contains(interest_group.name())) {
        ar_interest_group_index.add_index(ig_index);
      }
      ig_index++;
    }
    bidding_groups.try_emplace(buyer, std::move(ar_interest_group_index));
  }
  return bidding_groups;
}

}  // namespace privacy_sandbox::bidding_auction_servers
