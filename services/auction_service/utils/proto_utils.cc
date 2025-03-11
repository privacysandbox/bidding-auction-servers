// Copyright 2024 Google LLC
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

#include "services/auction_service/utils/proto_utils.h"

#include "absl/container/flat_hash_set.h"
#include "rapidjson/error/en.h"
#include "rapidjson/pointer.h"
#include "rapidjson/writer.h"
#include "services/common/util/json_util.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using ::google::protobuf::RepeatedPtrField;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;
using GhostWinnerForTopLevelAuction =
    AuctionResult::KAnonGhostWinner::GhostWinnerForTopLevelAuction;
// Builds a map of render urls to JSON objects holding the scoring signals.
// An entry looks like: url_to_signals["fooAds.com/123"] = {"fooAds.com/123":
// {"some", "scoring", "signals"}}. Notice the render URL is present in the map
// both as its key, and again in each entry as a key to the JSON object. This is
// intentional, as it allows moving the key to the new signals objects being
// built for each AdWithBid.
absl::flat_hash_map<std::string, rapidjson::Document> BuildAdScoringSignalsMap(
    rapidjson::Value& trusted_scoring_signals_value) {
  absl::flat_hash_map<std::string, rapidjson::Document> url_to_signals;
  for (rapidjson::Value::MemberIterator itr =
           trusted_scoring_signals_value.MemberBegin();
       itr != trusted_scoring_signals_value.MemberEnd(); ++itr) {
    // Moving the name will render it inaccessible unless a copy is made first.
    std::string ad_url = itr->name.GetString();
    // A Document rather than a Value is created, as a Document has an
    // Allocator.
    rapidjson::Document ad_details;
    ad_details.SetObject();
    // AddMember moves itr's Values, do not reference them anymore.
    ad_details.AddMember(itr->name, itr->value, ad_details.GetAllocator());
    url_to_signals.try_emplace(std::move(ad_url), std::move(ad_details));
  }
  return url_to_signals;
}

// Builds a set of ad component render urls for components used in more than one
// ad with bid. Since a single ad component might be a part of multiple ads, and
// since rapidjson enforces moves over copies unless explicitly specified
// otherwise (that's what makes it "rapid"), we need to know which ad component
// scoring signals need to be copied rather than moved. Otherwise, for an ad
// component used n times, moving its signals would fail on all times except the
// first. O(n) operation, where n is the number of ad component render urls.
absl::flat_hash_set<std::string> FindSharedComponentUrls(
    const google::protobuf::RepeatedPtrField<AdWithBidMetadata>&
        ads_with_bids) {
  absl::flat_hash_map<std::string, int> url_occurrences;
  absl::flat_hash_set<std::string> multiple_occurrence_component_urls;
  for (const auto& ad_with_bid : ads_with_bids) {
    for (const auto& ad_component_render_url : ad_with_bid.ad_components()) {
      ++url_occurrences[ad_component_render_url];
      if (url_occurrences[ad_component_render_url] > 1) {
        std::string copy_of_ad_component_render_url = ad_component_render_url;
        multiple_occurrence_component_urls.emplace(
            std::move(copy_of_ad_component_render_url));
      }
    }
  }
  return multiple_occurrence_component_urls;
}

// Adds scoring signals for ad component render urls to signals object
// for a single ad with bid, `ad_with_bid`.
// `ad_with_bid` and `multiple_occurrence_component_urls` will only
// be read from. `component_signals` must be filled with the trusted scoring
// signals for this ad's ad component render urls. If an ad component render url
// is not in `multiple_occurrence_component_urls`, then its signals will be
// moved out from `component signals`. Else they will be copied for use here and
// left intact for use in future calls to this method.
// Returns a JSON document that contains the scoring signals for all ad
// component render URLs in this ad_with_bid.
absl::StatusOr<rapidjson::Document> AddComponentSignals(
    const AdWithBidMetadata& ad_with_bid,
    const absl::flat_hash_set<std::string>& multiple_occurrence_component_urls,
    absl::flat_hash_map<std::string, rapidjson::Document>& component_signals,
    int& total_signals_added) {
  // Create overall signals object.
  rapidjson::Document combined_signals_for_this_bid;
  combined_signals_for_this_bid.SetObject();
  // Create empty expandable object to add ad component signals to.
  rapidjson::Document empty_object;
  empty_object.SetObject();
  // Add the expandable object to the combined signals object.
  auto combined_signals_itr =
      combined_signals_for_this_bid
          .AddMember(kAdComponentRenderUrlsProperty, empty_object,
                     combined_signals_for_this_bid.GetAllocator())
          .MemberBegin();

  for (const auto& ad_component_render_url : ad_with_bid.ad_components()) {
    // Check if there are signals for this component ad.
    auto component_url_signals_itr =
        component_signals.find(ad_component_render_url);
    if (component_url_signals_itr == component_signals.end()) {
      continue;
    }
    // Copy only if necessary.
    rapidjson::Document to_add;
    if (multiple_occurrence_component_urls.contains(ad_component_render_url)) {
      // Use the allocator of the object to which these signals are
      // ultimately going.
      to_add.CopyFrom(component_url_signals_itr->second,
                      combined_signals_for_this_bid.GetAllocator());
    } else {
      to_add.Swap(component_url_signals_itr->second);
    }
    // Grab member to avoid finding it twice.
    auto comp_url_signals_to_move_from =
        to_add.FindMember(ad_component_render_url.c_str());
    // This should never happen, the map entry's signals should always
    // be for the URL on which the map is keyed.
    if (comp_url_signals_to_move_from == to_add.MemberEnd()) {
      // This can only be caused by an error forming the map on our
      // side.
      return absl::Status(
          absl::StatusCode::kInternal,
          "Internal error while processing trusted scoring signals.");
    }
    // AddMember moves the values input into it, do not reference them
    // anymore.
    combined_signals_itr->value.AddMember(
        comp_url_signals_to_move_from->name,
        comp_url_signals_to_move_from->value,
        combined_signals_for_this_bid.GetAllocator());
    total_signals_added++;
  }
  return combined_signals_for_this_bid;
}

// Create bid metadata json string but doesn't close the json object
// for adding more fields.
std::string MakeOpenBidMetadataJson(
    absl::string_view publisher_hostname,
    absl::string_view interest_group_owner, absl::string_view render_url,
    const google::protobuf::RepeatedPtrField<std::string>&
        ad_component_render_urls,
    absl::string_view bid_currency, const uint32_t seller_data_version,
    ReportingIdsParamForBidMetadata reporting_ids = {}) {
  std::string bid_metadata = "{";
  if (!interest_group_owner.empty()) {
    absl::StrAppend(&bid_metadata, R"JSON(")JSON", kIGOwnerPropertyForScoreAd,
                    R"JSON(":")JSON", interest_group_owner, R"JSON(",)JSON");
  }
  if (!publisher_hostname.empty()) {
    absl::StrAppend(&bid_metadata, R"JSON(")JSON",
                    kTopWindowHostnamePropertyForScoreAd, R"JSON(":")JSON",
                    publisher_hostname, R"JSON(",)JSON");
  }
  if (!ad_component_render_urls.empty()) {
    absl::StrAppend(&bid_metadata, R"("adComponents":[)");
    for (int i = 0; i < ad_component_render_urls.size(); i++) {
      absl::StrAppend(&bid_metadata, "\"", ad_component_render_urls.at(i),
                      "\"");
      if (i != ad_component_render_urls.size() - 1) {
        absl::StrAppend(&bid_metadata, ",");
      }
    }
    absl::StrAppend(&bid_metadata, R"(],)");
  }
  if (!bid_currency.empty()) {
    absl::StrAppend(&bid_metadata, R"JSON(")JSON",
                    kBidCurrencyPropertyForScoreAd, R"JSON(":")JSON",
                    bid_currency, R"JSON(",)JSON");
  } else {
    absl::StrAppend(&bid_metadata, R"JSON(")JSON",
                    kBidCurrencyPropertyForScoreAd, R"JSON(":")JSON",
                    kUnknownBidCurrencyCode, R"JSON(",)JSON");
  }
  if (seller_data_version > 0) {
    absl::StrAppend(&bid_metadata, R"JSON(")JSON",
                    kSellerDataVersionPropertyForScoreAd, R"JSON(":)JSON",
                    seller_data_version, R"JSON(,)JSON");
  }
  if (reporting_ids.buyer_reporting_id) {
    absl::StrAppend(&bid_metadata, R"JSON(")JSON", kBuyerReportingIdForScoreAd,
                    R"JSON(":")JSON", reporting_ids.buyer_reporting_id.value(),
                    R"JSON(",)JSON");
  }
  if (reporting_ids.buyer_and_seller_reporting_id) {
    absl::StrAppend(&bid_metadata, R"JSON(")JSON",
                    kBuyerAndSellerReportingIdForScoreAd, R"JSON(":")JSON",
                    reporting_ids.buyer_and_seller_reporting_id.value(),
                    R"JSON(",)JSON");
  }
  if (reporting_ids.selected_buyer_and_seller_reporting_id) {
    absl::StrAppend(
        &bid_metadata, R"JSON(")JSON",
        kSelectedBuyerAndSellerReportingIdForScoreAd, R"JSON(":")JSON",
        reporting_ids.selected_buyer_and_seller_reporting_id.value(),
        R"JSON(",)JSON");
  }
  return bid_metadata;
}

// Gets the debug reporting URL (either win or loss URL) if the URL will not
// exceed the caps set within here.
inline absl::string_view GetDebugUrlIfInLimit(
    const std::string& url_type, const rapidjson::Value& debug_reporting_urls,
    int max_allowed_size_debug_url_chars,
    int max_allowed_size_all_debug_urls_chars,
    int current_all_debug_urls_chars) {
  auto debug_url_it = debug_reporting_urls.FindMember(url_type.c_str());
  if (debug_url_it == debug_reporting_urls.MemberEnd() ||
      !debug_url_it->value.IsString()) {
    return "";
  }

  absl::string_view debug_url = debug_url_it->value.GetString();
  int url_length = debug_url.length();
  if (url_length <= max_allowed_size_debug_url_chars &&
      url_length + current_all_debug_urls_chars <=
          max_allowed_size_all_debug_urls_chars) {
    PS_VLOG(8) << "Included the " << url_type << ": " << debug_url
               << " with length: " << url_length
               << ", previous total length: " << current_all_debug_urls_chars
               << ", new total length: "
               << current_all_debug_urls_chars + url_length;
    return debug_url;
  } else {
    PS_VLOG(8) << "Skipping " << url_type << " of length: " << url_length
               << ", since it will cause the new running total to become: "
               << current_all_debug_urls_chars + url_length
               << " (which surpasses the limit: "
               << max_allowed_size_all_debug_urls_chars << ")";
  }
  return "";
}

}  // namespace

void MayLogScoreAdsInput(const std::vector<std::shared_ptr<std::string>>& input,
                         RequestLogContext& log_context) {
  PS_VLOG(kDispatch, log_context)
      << "\n\nScore Ad Input Args:" << "\nAdMetadata:\n"
      << *(input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)]) << "\nBid:\n"
      << *(input[ScoreArgIndex(ScoreAdArgs::kBid)]) << "\nAuction Config:\n"
      << *(input[ScoreArgIndex(ScoreAdArgs::kAuctionConfig)])
      << "\nScoring Signals:\n"
      << *(input[ScoreArgIndex(ScoreAdArgs::kScoringSignals)])
      << "\nBid Metadata:\n"
      << *(input[ScoreArgIndex(ScoreAdArgs::kBidMetadata)])
      << "\nDirectFromSellerSignals:\n"
      << (input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)]);
}

std::shared_ptr<std::string> BuildAuctionConfig(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request) {
  return std::make_shared<std::string>(absl::StrCat(
      "{\"auctionSignals\": ",
      ((raw_request.auction_signals().empty()) ? "\"\""
                                               : raw_request.auction_signals()),
      ", ", "\"sellerSignals\": ",
      ((raw_request.seller_signals().empty()) ? "\"\""
                                              : raw_request.seller_signals()),
      "}"));
}

std::string MakeBidMetadata(
    absl::string_view publisher_hostname,
    absl::string_view interest_group_owner, absl::string_view render_url,
    const google::protobuf::RepeatedPtrField<std::string>&
        ad_component_render_urls,
    absl::string_view top_level_seller, absl::string_view bid_currency,
    const uint32_t seller_data_version,
    ReportingIdsParamForBidMetadata reporting_ids) {
  std::string bid_metadata =
      MakeOpenBidMetadataJson(publisher_hostname, interest_group_owner,
                              render_url, ad_component_render_urls,
                              bid_currency, seller_data_version, reporting_ids);
  // Only add top level seller to bid metadata if it's non empty.
  if (!top_level_seller.empty()) {
    absl::StrAppend(&bid_metadata, R"JSON(")JSON",
                    kTopLevelSellerFieldPropertyForScoreAd, R"JSON(":")JSON",
                    top_level_seller, R"JSON(",)JSON");
  }
  absl::StrAppend(&bid_metadata, R"JSON(")JSON", kRenderUrlsPropertyForScoreAd,
                  R"JSON(":")JSON", render_url, R"JSON("})JSON");
  return bid_metadata;
}

std::string MakeBidMetadataForTopLevelAuction(
    absl::string_view publisher_hostname,
    absl::string_view interest_group_owner, absl::string_view render_url,
    const google::protobuf::RepeatedPtrField<std::string>&
        ad_component_render_urls,
    absl::string_view component_seller, absl::string_view bid_currency,
    const uint32_t seller_data_version) {
  std::string bid_metadata = MakeOpenBidMetadataJson(
      publisher_hostname, interest_group_owner, render_url,
      ad_component_render_urls, bid_currency, seller_data_version);
  absl::StrAppend(&bid_metadata, R"JSON(")JSON",
                  kComponentSellerFieldPropertyForScoreAd, R"JSON(":")JSON",
                  component_seller, R"JSON(",)JSON");
  absl::StrAppend(&bid_metadata, R"JSON(")JSON", kRenderUrlsPropertyForScoreAd,
                  R"JSON(":")JSON", render_url, R"JSON("})JSON");
  return bid_metadata;
}

absl::StatusOr<absl::flat_hash_map<std::string, rapidjson::StringBuffer>>
BuildTrustedScoringSignals(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
    RequestLogContext& log_context,
    const bool require_scoring_signals_for_scoring) {
  rapidjson::Document trusted_scoring_signals_value;
  if (raw_request.scoring_signals().empty()) {
    return absl::InvalidArgumentError(kNoTrustedScoringSignals);
  }
  // Attempt to parse into an object.
  auto start_parse_time = absl::Now();
  rapidjson::ParseResult parse_result =
      trusted_scoring_signals_value.Parse<rapidjson::kParseFullPrecisionFlag>(
          raw_request.scoring_signals().data());
  if (parse_result.IsError()) {
    // TODO (b/285215004): Print offset to ease debugging.
    PS_VLOG(kNoisyWarn, log_context)
        << "Trusted scoring signals JSON parse error: "
        << rapidjson::GetParseError_En(parse_result.Code())
        << ", trusted signals were: " << raw_request.scoring_signals();
    return absl::InvalidArgumentError("Malformed trusted scoring signals");
  } else if (!trusted_scoring_signals_value.IsObject()) {
    // TODO (b/285215004): Print offset to ease debugging.
    PS_VLOG(kNoisyWarn, log_context)
        << "Trusted scoring signals JSON did not parse to a JSON object, "
           "trusted signals were: "
        << raw_request.scoring_signals();
    return absl::InvalidArgumentError("Malformed trusted scoring signals");
  }
  // Build a map of the signals for each render URL.
  auto render_urls_itr = trusted_scoring_signals_value.FindMember(
      kRenderUrlsPropertyForKVResponse);
  if (require_scoring_signals_for_scoring &&
      render_urls_itr == trusted_scoring_signals_value.MemberEnd()) {
    return absl::InvalidArgumentError(
        "Trusted scoring signals are required but include no render urls.");
  }
  absl::flat_hash_map<std::string, rapidjson::Document> render_url_signals;
  if (render_urls_itr != trusted_scoring_signals_value.MemberEnd()) {
    render_url_signals = BuildAdScoringSignalsMap(render_urls_itr->value);
  }
  // No scoring signals for ad component render urls are required,
  // however if present we build a map to their scoring signals in the same
  // way.
  absl::flat_hash_map<std::string, rapidjson::Document> component_signals;
  auto component_urls_itr =
      trusted_scoring_signals_value.FindMember(kAdComponentRenderUrlsProperty);
  if (component_urls_itr != trusted_scoring_signals_value.MemberEnd()) {
    component_signals = BuildAdScoringSignalsMap(component_urls_itr->value);
  }

  // Find the ad component render urls used more than once so we know which
  // signals we must copy rather than move.
  absl::flat_hash_set<std::string> multiple_occurrence_component_urls =
      FindSharedComponentUrls(raw_request.ad_bids());
  // Each AdWithBid needs signals for both its render URL and its ad component
  // render urls.
  absl::flat_hash_map<std::string, rapidjson::Document> combined_signals;
  for (const auto& ad_with_bid : raw_request.ad_bids()) {
    // Now that we have a map of all component signals and all ad signals, we
    // can build the object.
    int total_signals_added = 0;
    absl::StatusOr<rapidjson::Document> combined_signals_for_this_bid;
    PS_ASSIGN_OR_RETURN(
        combined_signals_for_this_bid,
        AddComponentSignals(ad_with_bid, multiple_occurrence_component_urls,
                            component_signals, total_signals_added));
    // Check for the render URL's signals; skip if none.
    auto render_url_signals_itr = render_url_signals.find(ad_with_bid.render());
    if (render_url_signals_itr != render_url_signals.end()) {
      // Do not reference values after move.
      combined_signals_for_this_bid->AddMember(
          kRenderUrlsPropertyForScoreAd, render_url_signals_itr->second,
          combined_signals_for_this_bid->GetAllocator());
      total_signals_added++;
    }
    if (total_signals_added > 0) {
      combined_signals.try_emplace(ad_with_bid.render(),
                                   *std::move(combined_signals_for_this_bid));
    }
  }

  MayPopulateScoringSignalsForProtectedAppSignals(
      raw_request, render_url_signals, combined_signals, log_context);

  // Now turn the editable JSON documents into string buffers before
  // returning.
  absl::flat_hash_map<std::string, rapidjson::StringBuffer>
      combined_formatted_ad_signals;
  for (const auto& [render_url, scoring_signals_json_obj] : combined_signals) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    scoring_signals_json_obj.Accept(writer);
    combined_formatted_ad_signals.try_emplace(render_url, std::move(buffer));
  }

  PS_VLOG(kStats, log_context)
      << "\nTrusted Scoring Signals Deserialize Time: "
      << ToInt64Microseconds((absl::Now() - start_parse_time))
      << " microseconds for " << combined_formatted_ad_signals.size()
      << " signals.";
  return combined_formatted_ad_signals;
}

void MayPopulateScoringSignalsForProtectedAppSignals(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request,
    absl::flat_hash_map<std::string, rapidjson::Document>& render_url_signals,
    absl::flat_hash_map<std::string, rapidjson::Document>& combined_signals,
    RequestLogContext& log_context) {
  PS_VLOG(8, log_context) << __func__;
  for (const auto& protected_app_signals_ad_bid :
       raw_request.protected_app_signals_ad_bids()) {
    auto it = render_url_signals.find(protected_app_signals_ad_bid.render());
    if (it == render_url_signals.end()) {
      PS_VLOG(5, log_context)
          << "Skipping protected app signals ad since render "
             "URL is not found in the scoring signals: "
          << protected_app_signals_ad_bid.render();
      continue;
    }

    rapidjson::Document combined_signals_for_this_bid(rapidjson::kObjectType);
    combined_signals_for_this_bid.AddMember(
        kRenderUrlsPropertyForScoreAd, it->second,
        combined_signals_for_this_bid.GetAllocator());
    const auto& [unused_it, succeeded] =
        combined_signals.try_emplace(protected_app_signals_ad_bid.render(),
                                     std::move(combined_signals_for_this_bid));
    if (!succeeded) {
      PS_LOG(ERROR, log_context) << "Render URL overlaps between bids: "
                                 << protected_app_signals_ad_bid.render();
    }
  }
}

absl::StatusOr<rapidjson::Document> ParseAndGetScoreAdResponseJson(
    bool enable_ad_tech_code_logging, RequestLogContext& log_context,
    const rapidjson::Document& score_ads_wrapper_response) {
  MayVlogAdTechCodeLogs(enable_ad_tech_code_logging, score_ads_wrapper_response,
                        log_context);
  rapidjson::Document response_obj;
  auto iterator = score_ads_wrapper_response.FindMember("response");
  if (iterator != score_ads_wrapper_response.MemberEnd()) {
    if (iterator->value.IsObject()) {
      response_obj.CopyFrom(iterator->value, response_obj.GetAllocator());
    } else if (iterator->value.IsNumber()) {
      response_obj.SetDouble(iterator->value.GetDouble());
    }
  }
  return response_obj;
}

ScoreAdsResponse::AdScore::AdRejectionReason BuildAdRejectionReason(
    absl::string_view interest_group_owner,
    absl::string_view interest_group_name,
    SellerRejectionReason seller_rejection_reason) {
  ScoreAdsResponse::AdScore::AdRejectionReason ad_rejection_reason;
  ad_rejection_reason.set_interest_group_owner(interest_group_owner);
  ad_rejection_reason.set_interest_group_name(interest_group_name);
  ad_rejection_reason.set_rejection_reason(seller_rejection_reason);
  return ad_rejection_reason;
}

std::optional<ScoreAdsResponse::AdScore::AdRejectionReason>
ParseAdRejectionReason(const rapidjson::Document& score_ad_resp,
                       absl::string_view interest_group_owner,
                       absl::string_view interest_group_name,
                       RequestLogContext& log_context) {
  auto reject_reason_itr =
      score_ad_resp.FindMember(kRejectReasonPropertyForScoreAd);
  if (reject_reason_itr == score_ad_resp.MemberEnd() ||
      !reject_reason_itr->value.IsString()) {
    return std::nullopt;
  }
  std::string rejection_reason_str =
      score_ad_resp[kRejectReasonPropertyForScoreAd].GetString();
  return BuildAdRejectionReason(interest_group_owner, interest_group_name,
                                ToSellerRejectionReason(rejection_reason_str));
}

absl::StatusOr<ScoreAdsResponse::AdScore> ScoreAdResponseJsonToProto(
    const rapidjson::Document& score_ad_resp,
    int max_allowed_size_debug_url_chars,
    int64_t max_allowed_size_all_debug_urls_chars,
    bool device_component_auction, int64_t& current_all_debug_urls_chars) {
  ScoreAdsResponse::AdScore score_ads_response;
  // Default value.
  score_ads_response.set_allow_component_auction(false);
  if (score_ad_resp.IsNumber()) {
    score_ads_response.set_desirability(score_ad_resp.GetDouble());
    return score_ads_response;
  }

  auto desirability_itr =
      score_ad_resp.FindMember(kDesirabilityPropertyForScoreAd);
  if (desirability_itr == score_ad_resp.MemberEnd() ||
      !desirability_itr->value.IsNumber()) {
    score_ads_response.set_desirability(0.0);
  } else {
    absl::StatusOr<float> parsed_desirability =
        desirability_itr->value.GetFloat();
    if (parsed_desirability.ok()) {
      score_ads_response.set_desirability(parsed_desirability.value());
    }
  }

  auto incoming_bid_in_seller_currency_itr =
      score_ad_resp.FindMember(kIncomingBidInSellerCurrency);
  if (incoming_bid_in_seller_currency_itr != score_ad_resp.MemberEnd() &&
      incoming_bid_in_seller_currency_itr->value.IsNumber()) {
    absl::StatusOr<float> parsed_incoming_bid_in_seller_currency =
        incoming_bid_in_seller_currency_itr->value.GetFloat();
    if (parsed_incoming_bid_in_seller_currency.ok()) {
      score_ads_response.set_incoming_bid_in_seller_currency(
          parsed_incoming_bid_in_seller_currency.value());
    }
  }

  // Parse component auction fields.
  // For now this is only valid for ProtectedAuction ads.
  if (device_component_auction) {
    auto component_auction_itr =
        score_ad_resp.FindMember(kAllowComponentAuctionPropertyForScoreAd);
    if (component_auction_itr != score_ad_resp.MemberEnd() &&
        component_auction_itr->value.IsBool()) {
      score_ads_response.set_allow_component_auction(
          component_auction_itr->value.GetBool());
    }

    auto ad_metadata_itr =
        score_ad_resp.FindMember(kAdMetadataForComponentAuction);
    if (ad_metadata_itr != score_ad_resp.MemberEnd() &&
        ad_metadata_itr->value.IsString()) {
      score_ads_response.set_ad_metadata(
          absl::StrCat("\"", ad_metadata_itr->value.GetString(), "\""));
    }

    if (ad_metadata_itr != score_ad_resp.MemberEnd() &&
        ad_metadata_itr->value.IsObject()) {
      PS_ASSIGN_OR_RETURN((*score_ads_response.mutable_ad_metadata()),
                          SerializeJsonDoc(ad_metadata_itr->value));
    }

    auto modified_bid_itr =
        score_ad_resp.FindMember(kModifiedBidForComponentAuction);
    if (modified_bid_itr != score_ad_resp.MemberEnd() &&
        modified_bid_itr->value.IsNumber()) {
      score_ads_response.set_bid((float)modified_bid_itr->value.GetFloat());
    }

    auto currency_of_modified_bid_itr =
        score_ad_resp.FindMember(kBidCurrencyPropertyForScoreAd);
    if (currency_of_modified_bid_itr != score_ad_resp.MemberEnd() &&
        currency_of_modified_bid_itr->value.IsString()) {
      absl::StatusOr<absl::string_view> parsed_currency_of_modified_bid =
          currency_of_modified_bid_itr->value.GetString();
      if (parsed_currency_of_modified_bid.ok()) {
        score_ads_response.set_bid_currency(
            parsed_currency_of_modified_bid.value());
      }
    }
  }

  auto debug_report_urls_itr =
      score_ad_resp.FindMember(kDebugReportUrlsPropertyForScoreAd);
  if (debug_report_urls_itr == score_ad_resp.MemberEnd()) {
    return score_ads_response;
  }

  DebugReportUrls debug_report_urls;
  const auto& debug_reporting_urls = debug_report_urls_itr->value;

  absl::string_view win_debug_url = GetDebugUrlIfInLimit(
      kAuctionDebugWinUrlPropertyForScoreAd, debug_reporting_urls,
      max_allowed_size_debug_url_chars, max_allowed_size_all_debug_urls_chars,
      current_all_debug_urls_chars);
  if (!win_debug_url.empty()) {
    debug_report_urls.set_auction_debug_win_url(win_debug_url);
    current_all_debug_urls_chars += win_debug_url.size();
  }
  absl::string_view loss_debug_url = GetDebugUrlIfInLimit(
      kAuctionDebugLossUrlPropertyForScoreAd, debug_reporting_urls,
      max_allowed_size_debug_url_chars, max_allowed_size_all_debug_urls_chars,
      current_all_debug_urls_chars);
  if (!loss_debug_url.empty()) {
    debug_report_urls.set_auction_debug_loss_url(loss_debug_url);
    current_all_debug_urls_chars += loss_debug_url.size();
  }
  *score_ads_response.mutable_debug_report_urls() =
      std::move(debug_report_urls);
  return score_ads_response;
}

absl::StatusOr<DispatchRequest> BuildScoreAdRequest(
    absl::string_view ad_render_url, absl::string_view ad_metadata_json,
    absl::string_view scoring_signals, float ad_bid,
    const std::shared_ptr<std::string>& auction_config,
    absl::string_view bid_metadata, RequestLogContext& log_context,
    const bool enable_adtech_code_logging, const bool enable_debug_reporting,
    absl::string_view code_version) {
  // Construct the wrapper struct for our V8 Dispatch Request.
  DispatchRequest score_ad_request;
  // TODO(b/250893468) Revisit dispatch id.
  score_ad_request.id = ad_render_url;
  // TODO(b/258790164) Update after code is fetched periodically.
  score_ad_request.version_string = code_version;
  score_ad_request.handler_name = DispatchHandlerFunctionWithSellerWrapper;

  score_ad_request.input = std::vector<std::shared_ptr<std::string>>(
      kArgSizeWithWrapper);  // ScoreAdArgs size

  score_ad_request.input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)] =
      std::make_shared<std::string>(ad_metadata_json);
  score_ad_request.input[ScoreArgIndex(ScoreAdArgs::kBid)] =
      std::make_shared<std::string>(std::to_string(ad_bid));
  score_ad_request.input[ScoreArgIndex(ScoreAdArgs::kAuctionConfig)] =
      auction_config;
  // TODO(b/258697130): Roma client string support bug
  score_ad_request.input[ScoreArgIndex(ScoreAdArgs::kScoringSignals)] =
      std::make_shared<std::string>(scoring_signals);
  score_ad_request.input[ScoreArgIndex(ScoreAdArgs::kBidMetadata)] =
      std::make_shared<std::string>(bid_metadata);
  // This is only added to prevent errors in the score ad script, and
  // will always be an empty object.
  score_ad_request.input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)] =
      std::make_shared<std::string>("{}");
  score_ad_request.input[ScoreArgIndex(ScoreAdArgs::kFeatureFlags)] =
      std::make_shared<std::string>(GetFeatureFlagJson(
          enable_adtech_code_logging, enable_debug_reporting));

  MayLogScoreAdsInput(score_ad_request.input, log_context);
  return score_ad_request;
}

std::unique_ptr<AdWithBidMetadata> MapAuctionResultToAdWithBidMetadata(
    AuctionResult& auction_result, bool k_anon_status) {
  auto ad = std::make_unique<AdWithBidMetadata>();
  ad->set_bid(auction_result.bid());
  ad->set_allocated_render(auction_result.release_ad_render_url());
  ad->mutable_ad_components()->Swap(
      auction_result.mutable_ad_component_render_urls());
  ad->set_allocated_interest_group_name(
      auction_result.release_interest_group_name());
  ad->set_allocated_interest_group_owner(
      auction_result.release_interest_group_owner());
  ad->set_allocated_bid_currency(auction_result.release_bid_currency());
  ad->set_k_anon_status(k_anon_status);
  return ad;
}

std::unique_ptr<
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata>
MapAuctionResultToProtectedAppSignalsAdWithBidMetadata(
    AuctionResult& auction_result, bool k_anon_status) {
  auto ad = std::make_unique<ProtectedAppSignalsAdWithBidMetadata>();
  ad->set_bid(auction_result.bid());
  ad->set_allocated_render(auction_result.release_ad_render_url());
  ad->set_allocated_owner(auction_result.release_interest_group_owner());
  ad->set_allocated_bid_currency(auction_result.release_bid_currency());
  ad->set_k_anon_status(k_anon_status);
  // Do not set egress payload since that will be included
  // in the component reporting signals.
  return ad;
}

std::unique_ptr<AdWithBidMetadata> MapKAnonGhostWinnerToAdWithBidMetadata(
    absl::string_view owner, absl::string_view ig_name,
    GhostWinnerForTopLevelAuction& ghost_winner) {
  auto ad = std::make_unique<AdWithBidMetadata>();
  ad->set_bid(ghost_winner.modified_bid());
  ad->set_allocated_render(ghost_winner.release_ad_render_url());
  ad->mutable_ad_components()->Swap(
      ghost_winner.mutable_ad_component_render_urls());
  ad->set_interest_group_name(ig_name);
  ad->set_interest_group_owner(owner);
  ad->set_allocated_bid_currency(ghost_winner.release_bid_currency());
  ad->set_k_anon_status(false);
  return ad;
}

}  // namespace privacy_sandbox::bidding_auction_servers
