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

#ifndef SERVICES_COMMON_TEST_RANDOM_H_
#define SERVICES_COMMON_TEST_RANDOM_H_

#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "api/bidding_auction_servers.pb.h"
#include "api/udf/generate_bid_udf_interface.pb.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/test/app_test_utils.h"
#include "src/util/status_macro/status_macros.h"

// helper functions to generate random objects for testing
namespace privacy_sandbox::bidding_auction_servers {

using EncodedBuyerInputs = google::protobuf::Map<std::string, std::string>;
using InterestGroupForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;

std::string MakeARandomString();

std::string MakeARandomStringOfLength(size_t length);

std::string MakeARandomUrl();

absl::flat_hash_map<std::string, std::string> MakeARandomMap(int entries = 2);

int MakeARandomInt(int min, int max);

template <typename num_type>
num_type MakeARandomNumber(num_type min, num_type max) {
  std::default_random_engine curr_time_generator(ToUnixMillis(absl::Now()));
  return std::uniform_real_distribution<num_type>(min,
                                                  max)(curr_time_generator);
}

std::unique_ptr<google::protobuf::Struct> MakeARandomStruct(int num_fields);

absl::Status ProtoToJson(const google::protobuf::Message& proto,
                         std::string* json_output);

std::unique_ptr<std::string> MakeARandomStructJsonString(int num_fields);

google::protobuf::Struct MakeAnAd(absl::string_view render_url,
                                  absl::string_view metadata_key,
                                  int metadata_value);

// Consistent to aid latency benchmarking.
std::string MakeAFixedSetOfUserBiddingSignals(int num_ads);

google::protobuf::ListValue MakeARandomListOfStrings();

google::protobuf::ListValue MakeARandomListOfNumbers();

std::string MakeRandomPreviousWins(
    const google::protobuf::RepeatedPtrField<std::string>& ad_render_ids,
    bool set_times_to_one = false);

BrowserSignals MakeRandomBrowserSignalsForIG(
    const google::protobuf::RepeatedPtrField<std::string>& ad_render_ids);

// Must manually delete/take ownership of underlying pointer
std::unique_ptr<BuyerInput::InterestGroup> MakeAnInterestGroupSentFromDevice();

// Build bidding signals values for given key.
std::string MakeBiddingSignalsValuesForKey(const std::string& key);

// Build bidding signals for an interest group sent from device. Returns a JSON
// string. Example:
// {"keys": {"key1": ["val1", "val2"],
//           "key2": ["val3"]},
//  "perInterestGroupData": {"ig1": ["val1", "val2", "val3"]}}
std::string MakeBiddingSignalsForIGFromDevice(
    const BuyerInput::InterestGroup& interest_group);

// Build trusted bidding signals for an interest group for bidding. Returns a
// JSON string. Example:
// {"key1": ["val1", "val2"],
//  "key2": ["val3"]}
std::string MakeTrustedBiddingSignalsForIG(
    const InterestGroupForBidding& interest_group);

// Get bidding signals for all the IGs in given GenerateBidsRawRequest. Returns
// a JSON string. Example:
// {"keys": {"key1": ["val1", "val2"],
//           "key2": ["val3"]},
//  "perInterestGroupData": {"ig1": ["val1", "val2", "val3"]}
//                           "ig2": ["val3"]}}
std::string GetBiddingSignalsFromGenerateBidsRequest(
    const GenerateBidsRequest::GenerateBidsRawRequest& raw_request);

InterestGroupForBidding MakeAnInterestGroupForBiddingSentFromDevice();

// build_android_signals: If false, will insert random values into
// browser signals, otherwise will insert random values into android signals.
InterestGroupForBidding MakeARandomInterestGroupForBidding(
    bool build_android_signals,
    bool set_user_bidding_signals_to_empty_struct = false);

InterestGroupForBidding MakeALargeInterestGroupForBiddingForLatencyTesting();

InterestGroupForBidding MakeARandomInterestGroupForBiddingFromAndroid();

InterestGroupForBidding MakeARandomInterestGroupForBiddingFromBrowser();

GenerateBidsRequest::GenerateBidsRawRequest
MakeARandomGenerateBidsRawRequestForAndroid(
    bool enforce_kanon = false, int multi_bid_limit = kDefaultMultiBidLimit);

GenerateBidsRequest::GenerateBidsRawRequest
MakeARandomGenerateBidsRequestForBrowser(
    bool enforce_kanon = false, int multi_bid_limit = kDefaultMultiBidLimit);

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
MakeARandomAdWithBidMetadata(float min_bid, float max_bid,
                             int num_ad_components = 5);

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
MakeARandomAdWithBidMetadataWithRejectionReason(float min_bid, float max_bid,
                                                int num_ad_components = 5,
                                                int rejection_reason_index = 0);

DebugReportUrls MakeARandomDebugReportUrls();

WinReportingUrls::ReportingUrls MakeARandomReportingUrls(
    int intraction_entries = 10);

WinReportingUrls MakeARandomWinReportingUrls();

AdWithBid MakeARandomAdWithBid(float min_bid, float max_bid,
                               int num_ad_components = 5);

AdWithBid MakeARandomAdWithBid(int64_t seed, bool debug_reporting_enabled,
                               bool allow_component_auction);

roma_service::ProtectedAudienceBid MakeARandomRomaProtectedAudienceBid(
    int64_t seed, bool debug_reporting_enabled, bool allow_component_auction);

PrivateAggregateContribution MakeARandomPrivateAggregationContribution(
    int64_t seed);

GenerateBidsResponse::GenerateBidsRawResponse
MakeARandomGenerateBidsRawResponse();

ScoreAdsResponse::AdScore MakeARandomAdScore(
    int hob_buyer_entries = 0, int rejection_reason_ig_owners = 0,
    int rejection_reason_ig_per_owner = 0);

// Must manually delete/take ownership of underlying pointer
// build_android_signals: If false, will build browser signals instead.
std::unique_ptr<BuyerInput::InterestGroup> MakeARandomInterestGroup(
    bool for_android);

std::unique_ptr<BuyerInput::InterestGroup>
MakeARandomInterestGroupFromAndroid();

std::unique_ptr<BuyerInput::InterestGroup>
MakeARandomInterestGroupFromBrowser();

GetBidsRequest::GetBidsRawRequest MakeARandomGetBidsRawRequest();

GetBidsRequest MakeARandomGetBidsRequest();

template <typename T>
T MakeARandomProtectedAuctionInput(int num_buyers = 2) {
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  for (int i = 0; i < num_buyers; i++) {
    BuyerInput buyer_input;
    auto ig_with_two_ads = MakeAnInterestGroupSentFromDevice();
    buyer_input.mutable_interest_groups()->AddAllocated(
        ig_with_two_ads.release());
    buyer_inputs.emplace(absl::StrFormat("ad_tech_%d.com", i), buyer_input);
  }
  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_input =
      GetEncodedBuyerInputMap(buyer_inputs);
  T protected_auction_input;
  protected_auction_input.set_generation_id(MakeARandomString());
  *protected_auction_input.mutable_buyer_input() =
      *std::move(encoded_buyer_input);
  protected_auction_input.set_publisher_name(MakeARandomString());
  protected_auction_input.set_enable_debug_reporting(true);
  return protected_auction_input;
}

template <typename T>
SelectAdRequest MakeARandomSelectAdRequest(
    absl::string_view seller_domain_origin, const T& protected_auction_input,
    bool set_buyer_egid = false, bool set_seller_egid = false,
    absl::string_view seller_currency = "",
    absl::string_view buyer_currency = "") {
  SelectAdRequest request;
  request.mutable_auction_config()->set_seller_signals(absl::StrCat(
      "{\"seller_signal\": \"", MakeARandomString(), "\"}"));  // 3.

  request.mutable_auction_config()->set_auction_signals(
      absl::StrCat("{\"auction_signal\": \"", MakeARandomString(), "\"}"));

  request.mutable_auction_config()->set_seller(MakeARandomString());
  request.mutable_auction_config()->set_buyer_timeout_ms(1000);

  if (set_seller_egid) {
    request.mutable_auction_config()
        ->mutable_code_experiment_spec()
        ->set_seller_kv_experiment_group_id(MakeARandomInt(1000, 10000));
  }

  for (auto& buyer_input_pair : protected_auction_input.buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        buyer_input_pair.first;
    SelectAdRequest::AuctionConfig::PerBuyerConfig per_buyer_config = {};
    per_buyer_config.set_buyer_signals(MakeARandomString());
    if (set_buyer_egid) {
      per_buyer_config.set_buyer_kv_experiment_group_id(
          MakeARandomInt(1000, 10000));
    }
    if (!buyer_currency.empty()) {
      per_buyer_config.set_buyer_currency(buyer_currency);
    }
    request.mutable_auction_config()->mutable_per_buyer_config()->insert(
        {buyer_input_pair.first, per_buyer_config});
  }

  request.mutable_auction_config()->set_seller(seller_domain_origin);
  if (!seller_currency.empty()) {
    request.mutable_auction_config()->set_seller_currency(seller_currency);
  }
  request.set_client_type(CLIENT_TYPE_BROWSER);
  return request;
}

google::protobuf::Value MakeAStringValue(const std::string& v);

google::protobuf::Value MakeANullValue();

google::protobuf::Value MakeAListValue(
    const std::vector<google::protobuf::Value>& vec);

struct TestComponentAuctionResultData {
  absl::string_view test_component_seller;
  std::string generation_id;
  std::string test_ig_owner;
  std::string test_component_win_reporting_url;
  std::string test_component_report_result_url;
  std::string test_component_event;
  std::string test_component_interaction_reporting_url;
  std::vector<std::string> buyer_list;
};

BuyerInput MakeARandomBuyerInput();

ProtectedAuctionInput MakeARandomProtectedAuctionInput(ClientType client_type);

// Populates fields for a auction result object for a single seller auction.
AuctionResult MakeARandomSingleSellerAuctionResult(
    std::vector<std::string> buyer_list = {});

// Populates fields for a auction result object for a component seller auction.
AuctionResult MakeARandomComponentAuctionResultWithReportingUrls(
    const TestComponentAuctionResultData& component_auction_result_data);

// Populates fields for a auction result object for a component seller auction
// without reporting urls
AuctionResult MakeARandomComponentAuctionResult(
    std::string generation_id, std::string top_level_seller,
    std::vector<std::string> buyer_list = {});
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_RANDOM_H_
