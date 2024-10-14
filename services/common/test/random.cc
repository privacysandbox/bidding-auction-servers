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

#include "services/common/test/random.h"

#include <set>
#include <string>

#include "absl/strings/str_format.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kTestIgWithTwoAds[] =
    R"json({"ad_render_ids":["adg_id=134256827445&cr_id=594352291621&cv_id=4", "adg_id=134256827445&cr_id=605048329089&cv_id=2"],"browser_signals":{"joinCount":1,"bidCount":100,"prevWins":"[[1472425.0,{\"metadata\":[134256827485.0,594352291615.0,null,16996677067.0]}],[1475389.0,{\"metadata\":[134256827445.0,594352291621.0,null,16996677067.0]}],[1487572.0,{\"metadata\":[134256827445.0,605490292974.0,null,16996677067.0]}],[1451707.0,{\"metadata\":[134256827445.0,605048329092.0,null,16996677067.0]}],[1485996.0,{\"metadata\":[134256827445.0,605048329089.0,null,16996677067.0]}],[1450931.0,{\"metadata\":[134256827485.0,605490292980.0,null,16996677067.0]}],[1473069.0,{\"metadata\":[134256827485.0,605048328957.0,null,16996677067.0]}],[1461197.0,{\"metadata\":[134256827485.0,605048329080.0,null,16996677067.0]}]]"},"name":"1j1043317685"})json";

std::string MakeARandomString() {
  return std::to_string(ToUnixNanos(absl::Now()));
}

std::string MakeARandomStringOfLength(size_t length) {
  static const std::string alphanumeric =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::default_random_engine curr_time_generator(ToUnixMillis(absl::Now()));
  std::uniform_int_distribution<int> distribution(0, alphanumeric.size() - 1);
  std::string output(length, ' ');
  std::generate_n(output.begin(), length, [&] {
    return alphanumeric[distribution(curr_time_generator)];
  });
  return output;
}

std::string MakeARandomUrl() {
  return absl::StrCat("https://", MakeARandomString(), ".com");
}

absl::flat_hash_map<std::string, std::string> MakeARandomMap(int entries) {
  absl::flat_hash_map<std::string, std::string> output;
  for (int i = 0; i < entries; i++) {
    output.try_emplace(MakeARandomString(), MakeARandomString());
  }
  return output;
}

int MakeARandomInt(int min, int max) {
  std::default_random_engine curr_time_generator(ToUnixMillis(absl::Now()));
  return std::uniform_int_distribution<int>(min, max)(curr_time_generator);
}

std::unique_ptr<google::protobuf::Struct> MakeARandomStruct(int num_fields) {
  auto a_struct = std::make_unique<google::protobuf::Struct>();
  auto& map = *(a_struct->mutable_fields());
  for (int i = 0; i < num_fields; i++) {
    map[MakeARandomString()].set_string_value(MakeARandomString());
  }
  return a_struct;
}

absl::Status ProtoToJson(const google::protobuf::Message& proto,
                         std::string* json_output) {
  auto options = google::protobuf::util::JsonPrintOptions();
  options.preserve_proto_field_names = true;
  return google::protobuf::util::MessageToJsonString(proto, json_output,
                                                     options);
}

std::unique_ptr<std::string> MakeARandomStructJsonString(int num_fields) {
  std::unique_ptr<std::string> json_output = std::make_unique<std::string>();
  std::unique_ptr<google::protobuf::Struct> map = MakeARandomStruct(num_fields);
  if (auto status = ProtoToJson(*map, json_output.get()); !status.ok()) {
    return nullptr;
  }
  return json_output;
}

google::protobuf::Struct MakeAnAd(absl::string_view render_url,
                                  absl::string_view metadata_key,
                                  int metadata_value) {
  google::protobuf::Struct ad;
  google::protobuf::Value ad_render_url;
  ad_render_url.set_string_value(render_url);
  ad.mutable_fields()->try_emplace("renderUrl", ad_render_url);
  auto metadata_obj = MakeARandomStruct(0);
  google::protobuf::Value metadata_v;
  metadata_v.set_number_value(metadata_value);
  metadata_obj->mutable_fields()->try_emplace(metadata_key, metadata_v);
  google::protobuf::Value metadata_obj_val;
  metadata_obj_val.set_allocated_struct_value(metadata_obj.release());
  ad.mutable_fields()->try_emplace("metadata", metadata_obj_val);
  return ad;
}

// Consistent to aid latency benchmarking.
std::string MakeAFixedSetOfUserBiddingSignals(int num_ads) {
  std::string output = "{";
  for (int i = 0; i < num_ads; i++) {
    absl::StrAppend(&output, "\"userBiddingSignal", i + 1, "\": \"someValue\"");
    if (i < num_ads - 1) {
      absl::StrAppend(&output, ",");
    }
  }
  absl::StrAppend(&output, "}");
  return output;
}

google::protobuf::ListValue MakeARandomListOfStrings() {
  google::protobuf::ListValue output;
  for (int i = 0; i < MakeARandomInt(1, 10); i++) {
    auto* v = output.mutable_values()->Add();
    v->set_string_value(MakeARandomString());
  }
  return output;
}

google::protobuf::ListValue MakeARandomListOfNumbers() {
  google::protobuf::ListValue output;
  for (int i = 0; i < MakeARandomInt(1, 10); i++) {
    float number = MakeARandomNumber<float>(1, 10);
    output.add_values()->set_number_value(number);
  }
  return output;
}

std::string MakeRandomPreviousWins(
    const google::protobuf::RepeatedPtrField<std::string>& ad_render_ids,
    bool set_times_to_one) {
  std::string previous_wins = "[";
  for (int i = 0; i < ad_render_ids.size(); i++) {
    int time_val = 1;
    if (!set_times_to_one) {
      time_val = (unsigned)time(NULL);
    }
    absl::StrAppend(&previous_wins, "[", time_val, ",\"", ad_render_ids.at(i),
                    "\"]");
    if (i < ad_render_ids.size() - 1) {
      absl::StrAppend(&previous_wins, ",");
    }
  }
  absl::StrAppend(&previous_wins, "]");
  return previous_wins;
}

BrowserSignals MakeRandomBrowserSignalsForIG(
    const google::protobuf::RepeatedPtrField<std::string>& ad_render_ids) {
  BrowserSignals browser_signals;
  browser_signals.set_join_count(MakeARandomInt(0, 10));
  browser_signals.set_bid_count(MakeARandomInt(0, 50));
  browser_signals.set_recency(60);  // secs
  browser_signals.set_recency_ms(60000);
  browser_signals.set_prev_wins(MakeRandomPreviousWins(ad_render_ids));
  return browser_signals;
}

// Must manually delete/take ownership of underlying pointer
std::unique_ptr<BuyerInput::InterestGroup> MakeAnInterestGroupSentFromDevice() {
  // Parse an IG in the form sent from the device.
  BuyerInput::InterestGroup ig_with_two_ads;
  auto result = google::protobuf::util::JsonStringToMessage(kTestIgWithTwoAds,
                                                            &ig_with_two_ads);
  std::unique_ptr<BuyerInput::InterestGroup> u_ptr_to_ig =
      std::make_unique<BuyerInput::InterestGroup>(ig_with_two_ads);
  u_ptr_to_ig->mutable_bidding_signals_keys()->Add(MakeARandomString());
  google::protobuf::RepeatedPtrField<std::string> ad_render_ids;
  ad_render_ids.Add(MakeARandomString());
  u_ptr_to_ig->mutable_browser_signals()->CopyFrom(
      MakeRandomBrowserSignalsForIG(ad_render_ids));
  return u_ptr_to_ig;
}

std::string MakeBiddingSignalsValuesForKey(const std::string& key) {
  std::string values = "[";
  int length = key.back() - '0';
  if (length < 1) {
    length = 1;
  }
  if (length > 9) {
    length = 9;
  }
  for (int j = 0; j < length; j++) {
    absl::StrAppend(&values, absl::StrFormat(R"JSON("%s_val%d",)JSON", key, j));
  }
  absl::StrAppend(&values,
                  absl::StrFormat(R"JSON("%s_val%d"])JSON", key, length));
  return values;
}

std::string MakeBiddingSignalsForIGFromDevice(
    const BuyerInput::InterestGroup& interest_group) {
  std::string signals_dest;

  // First add the values for the bidding signals keys.
  absl::StrAppend(&signals_dest, absl::StrFormat(R"JSON({"keys":{)JSON"));
  for (int i = 0; i < interest_group.bidding_signals_keys_size(); i++) {
    const auto& bidding_signal_key = interest_group.bidding_signals_keys(i);

    // Append key-value pair.
    absl::StrAppend(
        &signals_dest,
        absl::StrFormat(R"JSON("%s":%s)JSON", bidding_signal_key,
                        MakeBiddingSignalsValuesForKey(bidding_signal_key)));

    // Add a comma for all but the last key-value pair in this IG.
    if (i < interest_group.bidding_signals_keys_size() - 1) {
      absl::StrAppend(&signals_dest, ",");
    }
  }

  // Then add the list of values for the IG.
  absl::StrAppend(&signals_dest,
                  absl::StrFormat(R"JSON(},"perInterestGroupData":{)JSON"));

  // Iterate through all of the trusted bidding signals keys and flatten their
  // values into raw_values.
  std::string raw_values = "[";
  for (int i = 0; i < interest_group.bidding_signals_keys_size(); i++) {
    const auto& bidding_signal_key = interest_group.bidding_signals_keys(i);

    // Read in values for this key and append them to raw_values without the
    // list brackets on either side.
    std::string raw_values_for_key =
        MakeBiddingSignalsValuesForKey(bidding_signal_key);
    absl::StrAppend(&raw_values,
                    absl::StrFormat(R"JSON(%s)JSON",
                                    raw_values_for_key.substr(
                                        1, raw_values_for_key.size() - 2)));

    // Unless we are at the final key, add the last comma for this key's list
    // of values.
    if (i < interest_group.bidding_signals_keys_size() - 1) {
      absl::StrAppend(&raw_values, ",");
    }
  }
  absl::StrAppend(&raw_values, "]");

  // Add the name-values pair for the IG.
  absl::StrAppend(&signals_dest,
                  absl::StrFormat(R"JSON("%s":%s}})JSON", interest_group.name(),
                                  raw_values));

  return signals_dest;
}

std::string MakeTrustedBiddingSignalsForIG(
    const InterestGroupForBidding& interest_group) {
  std::string signals_dest = "{";
  // Iterate over each bidding signal key.
  for (int i = 0; i < interest_group.trusted_bidding_signals_keys_size(); i++) {
    const auto& trusted_bidding_signal_key =
        interest_group.trusted_bidding_signals_keys(i);

    // Append key-value pair to trusted bidding signals JSON string.
    absl::StrAppend(
        &signals_dest,
        absl::StrFormat(
            R"JSON("%s":%s)JSON", trusted_bidding_signal_key,
            MakeBiddingSignalsValuesForKey(trusted_bidding_signal_key)));

    // Add a comma for all but the last key-value pair.
    if (i < interest_group.trusted_bidding_signals_keys_size() - 1) {
      absl::StrAppend(&signals_dest, ",");
    }
  }
  absl::StrAppend(&signals_dest, "}");
  return signals_dest;
}

std::string GetBiddingSignalsFromGenerateBidsRequest(
    const GenerateBidsRequest::GenerateBidsRawRequest& raw_request) {
  std::string signals_dest;

  // First add the values for the trusted bidding signals keys for each IG.
  absl::StrAppend(&signals_dest, absl::StrFormat(R"JSON({"keys":{)JSON"));
  for (int i = 0; i < raw_request.interest_group_for_bidding_size(); i++) {
    const auto& interest_group = raw_request.interest_group_for_bidding(i);

    // Read in key-value pairs for this IG and append them to signals_dest
    // without the JSON brackets on either side.
    absl::string_view raw_signals = interest_group.trusted_bidding_signals();
    absl::StrAppend(
        &signals_dest,
        absl::StrFormat(R"JSON(%s)JSON",
                        raw_signals.substr(1, raw_signals.size() - 2)));

    // Unless we are at the final IG, add the last comma for this IG's key-value
    // pairs.
    if (i < raw_request.interest_group_for_bidding_size() - 1) {
      absl::StrAppend(&signals_dest, ",");
    }
  }

  // Then add the list of values for each IG.
  absl::StrAppend(&signals_dest,
                  absl::StrFormat(R"JSON(},"perInterestGroupData":{)JSON"));
  for (int i = 0; i < raw_request.interest_group_for_bidding_size(); i++) {
    const auto& interest_group = raw_request.interest_group_for_bidding(i);

    // Iterate through all of the trusted bidding signals keys of this IG and
    // flatten their values into raw_values.
    std::string raw_values = "[";
    for (int j = 0; j < interest_group.trusted_bidding_signals_keys_size();
         j++) {
      const auto& trusted_bidding_signal_key =
          interest_group.trusted_bidding_signals_keys(j);

      // Read in values for this key and append them to raw_values without the
      // list brackets on either side.
      std::string raw_values_for_key =
          MakeBiddingSignalsValuesForKey(trusted_bidding_signal_key);
      absl::StrAppend(&raw_values,
                      absl::StrFormat(R"JSON(%s)JSON",
                                      raw_values_for_key.substr(
                                          1, raw_values_for_key.size() - 2)));

      // Unless we are at the final key, add the last comma for this key's list
      // of values.
      if (j < interest_group.trusted_bidding_signals_keys_size() - 1) {
        absl::StrAppend(&raw_values, ",");
      }
    }
    absl::StrAppend(&raw_values, "]");

    // Add the name-values pair for this IG.
    absl::StrAppend(&signals_dest,
                    absl::StrFormat(R"JSON("%s":%s)JSON", interest_group.name(),
                                    raw_values));

    // Unless we are at the final IG, add the last comma for this IG's
    // name-values pair.
    if (i < raw_request.interest_group_for_bidding_size() - 1) {
      absl::StrAppend(&signals_dest, ",");
    }
  }
  absl::StrAppend(&signals_dest, "}}");

  return signals_dest;
}

InterestGroupForBidding MakeAnInterestGroupForBiddingSentFromDevice() {
  std::unique_ptr<BuyerInput::InterestGroup> ig_with_two_ads =
      MakeAnInterestGroupSentFromDevice();
  InterestGroupForBidding ig_for_bidding_from_device;

  ig_for_bidding_from_device.set_name(ig_with_two_ads->name());
  ig_for_bidding_from_device.mutable_browser_signals()->CopyFrom(
      ig_with_two_ads->browser_signals());
  ig_for_bidding_from_device.mutable_ad_render_ids()->MergeFrom(
      ig_with_two_ads->ad_render_ids());
  ig_for_bidding_from_device.mutable_trusted_bidding_signals_keys()->Add(
      MakeARandomString());
  ig_for_bidding_from_device.set_trusted_bidding_signals(
      MakeTrustedBiddingSignalsForIG(ig_for_bidding_from_device));
  return ig_for_bidding_from_device;
}

InterestGroupForBidding MakeARandomInterestGroupForBidding(
    bool build_android_signals, bool set_user_bidding_signals_to_empty_struct) {
  InterestGroupForBidding ig_for_bidding;
  ig_for_bidding.set_name(absl::StrCat("ig_name_random_", MakeARandomString()));
  ig_for_bidding.mutable_trusted_bidding_signals_keys()->Add(
      absl::StrCat("trusted_bidding_signals_key_random_", MakeARandomString()));
  ig_for_bidding.set_trusted_bidding_signals(
      MakeTrustedBiddingSignalsForIG(ig_for_bidding));
  if (!set_user_bidding_signals_to_empty_struct) {
    // Takes ownership of pointer.
    ig_for_bidding.set_user_bidding_signals(
        R"JSON({"years": [1776, 1868], "name": "winston", "someId": 1789})JSON");
  }
  int ad_render_ids_to_generate = MakeARandomInt(1, 10);
  for (int i = 0; i < ad_render_ids_to_generate; i++) {
    *ig_for_bidding.mutable_ad_render_ids()->Add() =
        absl::StrCat("ad_render_id_random_", MakeARandomString());
  }
  int ad_component_render_ids_to_generate = MakeARandomInt(1, 10);
  for (int i = 0; i < ad_component_render_ids_to_generate; i++) {
    *ig_for_bidding.mutable_ad_component_render_ids()->Add() =
        absl::StrCat("ad_component_render_id_random_", MakeARandomString());
  }
  if (build_android_signals) {
    // Empty message for now.
    ig_for_bidding.mutable_android_signals();
  } else {
    ig_for_bidding.mutable_browser_signals()->CopyFrom(
        MakeRandomBrowserSignalsForIG(ig_for_bidding.ad_render_ids()));
  }
  return ig_for_bidding;
}

InterestGroupForBidding MakeALargeInterestGroupForBiddingForLatencyTesting() {
  int num_bidding_signals_keys = 10;
  int num_ad_render_ids = 10;
  int num_ad_component_render_ids = 10;
  int num_user_bidding_signals = 10;
  InterestGroupForBidding ig_for_bidding;
  // Name.
  ig_for_bidding.set_name("HandbagShoppers");
  // Ad render IDs.
  for (int i = 0; i < num_ad_render_ids; i++) {
    ig_for_bidding.mutable_ad_render_ids()->Add(
        absl::StrCat("adRenderId", i + 1));
  }
  // Ad component render IDs.
  for (int i = 0; i < num_ad_component_render_ids; i++) {
    ig_for_bidding.mutable_ad_component_render_ids()->Add(
        absl::StrCat("adComponentRenderId", i + 1));
  }
  // Bidding signals keys.
  for (int i = 0; i < num_bidding_signals_keys; i++) {
    ig_for_bidding.mutable_trusted_bidding_signals_keys()->Add(
        absl::StrCat("biddingSignalsKey", i + 1));
  }
  // Bidding signals.
  ig_for_bidding.set_trusted_bidding_signals(
      MakeTrustedBiddingSignalsForIG(ig_for_bidding));
  // User bidding signals.
  ig_for_bidding.set_user_bidding_signals(
      MakeAFixedSetOfUserBiddingSignals(num_user_bidding_signals));
  // Device signals.
  ig_for_bidding.mutable_browser_signals()->CopyFrom(
      MakeRandomBrowserSignalsForIG(ig_for_bidding.ad_render_ids()));
  return ig_for_bidding;
}

InterestGroupForBidding MakeARandomInterestGroupForBiddingFromAndroid() {
  return MakeARandomInterestGroupForBidding(true);
}

InterestGroupForBidding MakeARandomInterestGroupForBiddingFromBrowser() {
  return MakeARandomInterestGroupForBidding(false);
}

GenerateBidsRequest::GenerateBidsRawRequest
MakeARandomGenerateBidsRawRequestForAndroid(bool enforce_kanon,
                                            int multi_bid_limit) {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      MakeARandomInterestGroupForBiddingFromAndroid();
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      MakeARandomInterestGroupForBiddingFromAndroid();
  raw_request.set_allocated_auction_signals(
      std::move(MakeARandomStructJsonString(MakeARandomInt(0, 100))).release());
  raw_request.set_allocated_buyer_signals(
      std::move(MakeARandomStructJsonString(MakeARandomInt(0, 100))).release());

  raw_request.set_enforce_kanon(enforce_kanon);
  raw_request.set_multi_bid_limit(multi_bid_limit);

  return raw_request;
}

GenerateBidsRequest::GenerateBidsRawRequest
MakeARandomGenerateBidsRequestForBrowser(bool enforce_kanon,
                                         int multi_bid_limit) {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      MakeARandomInterestGroupForBiddingFromBrowser();
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      MakeARandomInterestGroupForBiddingFromBrowser();
  raw_request.set_allocated_auction_signals(
      std::move(MakeARandomStructJsonString(MakeARandomInt(0, 100))).release());
  raw_request.set_allocated_buyer_signals(
      std::move(MakeARandomStructJsonString(MakeARandomInt(0, 10))).release());
  raw_request.set_seller(MakeARandomString());
  raw_request.set_publisher_name(MakeARandomString());

  raw_request.set_enforce_kanon(enforce_kanon);
  raw_request.set_multi_bid_limit(multi_bid_limit);

  return raw_request;
}

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
MakeARandomAdWithBidMetadata(float min_bid, float max_bid,
                             int num_ad_components) {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad_with_bid;

  ad_with_bid.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), MakeARandomString(), 2));
  ad_with_bid.set_bid(MakeARandomNumber<float>(min_bid, max_bid));
  ad_with_bid.set_render(
      absl::StrCat("barStandardAds.com/ads?id=", MakeARandomString()));

  for (int i = 0; i < num_ad_components; i++) {
    ad_with_bid.add_ad_components(absl::StrCat("adComponent.com/id=", i));
  }
  // allow_component_auction defaults to false.

  ad_with_bid.set_interest_group_name(
      absl::StrCat("interest_group_name_", MakeARandomString()));
  ad_with_bid.set_interest_group_owner(
      absl::StrCat("interest_group_owner_", MakeARandomString()));

  // join_count and recency left to default zeros.

  ad_with_bid.set_modeling_signals(MakeARandomInt(0, 100));
  ad_with_bid.set_ad_cost(MakeARandomNumber<double>(0.0, 2.0));

  // No bid currency specified.

  ad_with_bid.set_interest_group_origin(
      absl::StrCat("interest_group_origin_", MakeARandomString()));

  return ad_with_bid;
}

ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata
MakeARandomAdWithBidMetadataWithRejectionReason(float min_bid, float max_bid,
                                                int num_ad_components,
                                                int rejection_reason_index) {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad_with_bid;
  ad_with_bid.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), "rejectReason", rejection_reason_index));
  ad_with_bid.set_interest_group_name(MakeARandomString());
  ad_with_bid.set_render(MakeARandomString());
  ad_with_bid.set_bid(MakeARandomNumber<float>(min_bid, max_bid));
  ad_with_bid.set_interest_group_owner(MakeARandomString());

  for (int i = 0; i < num_ad_components; i++) {
    ad_with_bid.add_ad_components(absl::StrCat("adComponent.com/id=", i));
  }

  return ad_with_bid;
}

DebugReportUrls MakeARandomDebugReportUrls() {
  DebugReportUrls debug_report_urls;
  debug_report_urls.set_auction_debug_win_url(MakeARandomUrl());
  debug_report_urls.set_auction_debug_loss_url(MakeARandomUrl());
  return debug_report_urls;
}

WinReportingUrls::ReportingUrls MakeARandomReportingUrls(
    int intraction_entries) {
  WinReportingUrls::ReportingUrls reporting_urls;
  reporting_urls.set_reporting_url(MakeARandomUrl());
  for (int index = 0; index < intraction_entries; index++) {
    reporting_urls.mutable_interaction_reporting_urls()->try_emplace(
        MakeARandomString(), MakeARandomUrl());
  }
  return reporting_urls;
}

WinReportingUrls MakeARandomWinReportingUrls() {
  WinReportingUrls win_reporting_urls;
  *win_reporting_urls.mutable_buyer_reporting_urls() =
      MakeARandomReportingUrls();
  *win_reporting_urls.mutable_component_seller_reporting_urls() =
      MakeARandomReportingUrls();
  *win_reporting_urls.mutable_top_level_seller_reporting_urls() =
      MakeARandomReportingUrls();
  return win_reporting_urls;
}

AdWithBid MakeARandomAdWithBid(float min_bid, float max_bid,
                               int num_ad_components) {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  AdWithBid ad_with_bid;

  ad_with_bid.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), MakeARandomString(), 2));
  ad_with_bid.set_interest_group_name(MakeARandomString());
  ad_with_bid.set_render(MakeARandomString());
  ad_with_bid.set_bid(MakeARandomNumber<float>(min_bid, max_bid));
  *ad_with_bid.mutable_debug_report_urls() = MakeARandomDebugReportUrls();
  for (int i = 0; i < num_ad_components; i++) {
    ad_with_bid.add_ad_components(absl::StrCat("adComponent.com/id=", i));
  }
  ad_with_bid.set_bid_currency("USD");
  ad_with_bid.set_ad_cost(MakeARandomNumber<double>(0.0, 2.0));
  ad_with_bid.set_modeling_signals(MakeARandomInt(0, 100));
  return ad_with_bid;
}

AdWithBid MakeARandomAdWithBid(int64_t seed, bool debug_reporting_enabled,
                               bool allow_component_auction) {
  std::string random_string = std::to_string(seed);
  std::string random_url = absl::StrCat("https://", random_string, ".com");

  AdWithBid bid;
  bid.mutable_ad()->set_string_value(random_string);
  bid.set_bid(1.1f + seed);
  bid.set_render(random_url);
  bid.add_ad_components(absl::StrFormat("%s/%d", random_url, seed));
  bid.add_ad_components(absl::StrFormat("%s/%d", random_url, seed + 1));
  bid.set_allow_component_auction(allow_component_auction);
  bid.set_interest_group_name(random_string);
  bid.set_ad_cost(0.5f + seed);
  bid.set_modeling_signals(seed);
  bid.set_bid_currency("USD");
  bid.set_buyer_reporting_id(absl::StrFormat("id_%s", random_string));
  if (debug_reporting_enabled) {
    bid.mutable_debug_report_urls()->set_auction_debug_win_url(
        absl::StrFormat("%s/win", random_url));
    bid.mutable_debug_report_urls()->set_auction_debug_loss_url(
        absl::StrFormat("%s/loss", random_url));
  }
  *bid.add_private_aggregation_contributions() =
      MakeARandomPrivateAggregationContribution(seed);
  return bid;
}

roma_service::ProtectedAudienceBid MakeARandomRomaProtectedAudienceBid(
    int64_t seed, bool debug_reporting_enabled, bool allow_component_auction) {
  std::string random_string = std::to_string(seed);
  std::string random_url = absl::StrCat("https://", random_string, ".com");

  roma_service::ProtectedAudienceBid bid;
  bid.set_ad(random_string);
  bid.set_bid(1.1f + seed);
  bid.set_render(random_url);
  bid.add_ad_components(absl::StrFormat("%s/%d", random_url, seed));
  bid.add_ad_components(absl::StrFormat("%s/%d", random_url, seed + 1));
  bid.set_allow_component_auction(allow_component_auction);
  bid.set_ad_cost(0.5f + seed);
  bid.set_modeling_signals(seed);
  bid.set_bid_currency("USD");
  bid.set_buyer_reporting_id(absl::StrFormat("id_%s", random_string));
  if (debug_reporting_enabled) {
    bid.mutable_debug_report_urls()->set_auction_debug_win_url(
        absl::StrFormat("%s/win", random_url));
    bid.mutable_debug_report_urls()->set_auction_debug_loss_url(
        absl::StrFormat("%s/loss", random_url));
  }
  return bid;
}

PrivateAggregateContribution MakeARandomPrivateAggregationContribution(
    int64_t seed) {
  PrivateAggregateContribution contribution;
  contribution.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      seed);
  contribution.mutable_value()->mutable_extended_value()->set_base_value(
      BaseValue::BASE_VALUE_WINNING_BID);
  contribution.mutable_value()->mutable_extended_value()->set_scale(2.0);
  contribution.mutable_value()->mutable_extended_value()->set_offset(-seed);
  return contribution;
}

GenerateBidsResponse::GenerateBidsRawResponse
MakeARandomGenerateBidsRawResponse() {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.mutable_bids()->Add(MakeARandomAdWithBid(0, 10));
  return raw_response;
}

UpdateInterestGroupList MakeAnUpdateInterestGroupList() {
  UpdateInterestGroupList list;
  for (int i = 0; i < 5; i++) {
    UpdateInterestGroup update;
    update.set_update_if_older_than_ms(i + 1000);
    update.set_index(i);
    *list.mutable_interest_groups()->Add() = std::move(update);
  }
  return list;
}

ScoreAdsResponse::AdScore MakeARandomAdScore(
    int hob_buyer_entries, int rejection_reason_ig_owners,
    int rejection_reason_ig_per_owner) {
  ScoreAdsResponse::AdScore ad_score;
  float bid = MakeARandomNumber<float>(1, 2.5);
  float score = MakeARandomNumber<float>(1, 2.5);
  ad_score.set_desirability(score);
  ad_score.set_render(MakeARandomString());
  ad_score.set_interest_group_name(MakeARandomString());
  ad_score.set_buyer_bid(bid);
  ad_score.set_interest_group_owner(MakeARandomString());
  ad_score.set_interest_group_origin(
      absl::StrCat("interest_group_origin_", MakeARandomString()));
  ad_score.set_ad_metadata(MakeARandomString());
  ad_score.set_allow_component_auction(false);
  ad_score.set_bid(MakeARandomNumber<float>(1, 2.5));
  *ad_score.mutable_debug_report_urls() = MakeARandomDebugReportUrls();
  *ad_score.mutable_win_reporting_urls() = MakeARandomWinReportingUrls();
  for (int index = 0; index < hob_buyer_entries; index++) {
    ad_score.mutable_ig_owner_highest_scoring_other_bids_map()->try_emplace(
        MakeARandomString(), MakeARandomListOfNumbers());
  }
  std::vector<ScoreAdsResponse::AdScore::AdRejectionReason>
      ad_rejection_reasons;
  for (int index = 0; index < rejection_reason_ig_owners; index++) {
    std::string interest_group_owner = MakeARandomString();
    for (int j = 0; j < rejection_reason_ig_per_owner; j++) {
      ScoreAdsResponse::AdScore::AdRejectionReason ad_rejection_reason;
      ad_rejection_reason.set_interest_group_name(MakeARandomString());
      ad_rejection_reason.set_interest_group_owner(interest_group_owner);
      int rejection_reason_value =
          MakeARandomInt(1, 7);  // based on number of rejection reasons.
      ad_rejection_reason.set_rejection_reason(
          static_cast<SellerRejectionReason>(rejection_reason_value));
      ad_rejection_reasons.push_back(ad_rejection_reason);
    }
  }
  *ad_score.mutable_ad_rejection_reasons() = {ad_rejection_reasons.begin(),
                                              ad_rejection_reasons.end()};
  return ad_score;
}

// Must manually delete/take ownership of underlying pointer
// build_android_signals: If false, will build browser signals instead.
std::unique_ptr<BuyerInput::InterestGroup> MakeARandomInterestGroup(
    bool for_android) {
  auto interest_group = std::make_unique<BuyerInput::InterestGroup>();
  interest_group->set_name(absl::StrCat("ig_name_", MakeARandomString()));

  interest_group->mutable_bidding_signals_keys()->Add(
      absl::StrCat("bidding_signal_key_", MakeARandomString()));
  interest_group->mutable_bidding_signals_keys()->Add(
      absl::StrCat("bidding_signal_key_", MakeARandomString()));

  int ad_render_ids_to_generate = MakeARandomInt(1, 10);
  for (int i = 0; i < ad_render_ids_to_generate; i++) {
    *interest_group->mutable_ad_render_ids()->Add() =
        absl::StrCat("ad_render_id_", MakeARandomString());
  }

  // No component ads added.

  interest_group->set_allocated_user_bidding_signals(
      MakeARandomStructJsonString(5).release());

  if (for_android) {
    // Empty field right now.
    interest_group->mutable_android_signals();
    interest_group->set_origin(
        absl::StrCat("interest_group_origin_", MakeARandomString()));
  } else {
    interest_group->mutable_browser_signals()->CopyFrom(
        MakeRandomBrowserSignalsForIG(interest_group->ad_render_ids()));
  }

  return interest_group;
}

std::unique_ptr<BuyerInput::InterestGroup>
MakeARandomInterestGroupFromAndroid() {
  return MakeARandomInterestGroup(/*for_android=*/true);
}

std::unique_ptr<BuyerInput::InterestGroup>
MakeARandomInterestGroupFromBrowser() {
  return MakeARandomInterestGroup(/*for_android=*/false);
}

GetBidsRequest::GetBidsRawRequest MakeARandomGetBidsRawRequest() {
  // request object will take ownership
  // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_publisher_name("publisher_name");
  raw_request.set_allocated_auction_signals(
      MakeARandomStructJsonString(1).release());
  return raw_request;
}

GetBidsRequest MakeARandomGetBidsRequest() {
  GetBidsRequest request;
  GetBidsRequest::GetBidsRawRequest raw_request =
      MakeARandomGetBidsRawRequest();
  request.set_request_ciphertext(raw_request.SerializeAsString());
  request.set_key_id(MakeARandomString());
  return request;
}

google::protobuf::Value MakeAStringValue(const std::string& v) {
  google::protobuf::Value obj;
  obj.set_string_value(v);
  return obj;
}

google::protobuf::Value MakeANullValue() {
  google::protobuf::Value obj;
  obj.set_null_value(google::protobuf::NULL_VALUE);
  return obj;
}

google::protobuf::Value MakeAListValue(
    const std::vector<google::protobuf::Value>& vec) {
  google::protobuf::Value obj;
  for (auto& val : vec) {
    obj.mutable_list_value()->add_values()->MergeFrom(val);
  }
  return obj;
}

BuyerInput MakeARandomBuyerInput() {
  BuyerInput buyer_input;
  buyer_input.mutable_interest_groups()->AddAllocated(
      MakeARandomInterestGroup(/*for_android=*/false).release());
  return buyer_input;
}

ProtectedAuctionInput MakeARandomProtectedAuctionInput(ClientType client_type) {
  ProtectedAuctionInput input;
  input.set_publisher_name(MakeARandomString());
  input.set_generation_id(MakeARandomString());
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.emplace(MakeARandomString(), MakeARandomBuyerInput());
  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_input;
  switch (client_type) {
    case CLIENT_TYPE_BROWSER:
      encoded_buyer_input = GetEncodedBuyerInputMap(buyer_inputs);
      break;
    case CLIENT_TYPE_ANDROID:
      encoded_buyer_input = GetProtoEncodedBuyerInputs(buyer_inputs);
      break;
    default:
      break;
  }
  *input.mutable_buyer_input() = *std::move(encoded_buyer_input);
  input.set_enable_debug_reporting(true);
  return input;
}

AuctionResult MakeARandomSingleSellerAuctionResult(
    std::vector<std::string> buyer_list) {
  AuctionResult result;
  result.set_ad_render_url(MakeARandomString());
  result.set_interest_group_name(MakeARandomString());
  result.set_interest_group_owner(MakeARandomString());
  result.set_score(MakeARandomNumber<float>(0.0, 1.0));

  if (buyer_list.empty()) {
    buyer_list.push_back(MakeARandomString());
  }
  for (const auto& buyer :
       std::set<std::string>(buyer_list.begin(), buyer_list.end())) {
    AuctionResult::InterestGroupIndex ig_indices;
    ig_indices.add_index(MakeARandomInt(1, 5));
    result.mutable_bidding_groups()->try_emplace(buyer, std::move(ig_indices));
    result.mutable_update_groups()->try_emplace(
        buyer, MakeAnUpdateInterestGroupList());
  }

  // TODO(b/287074572): Add reporting URLs and other reporting fields here
  // when adding support for reporting.
  return result;
}

AuctionResult MakeARandomComponentAuctionResultWithReportingUrls(
    const TestComponentAuctionResultData& component_auction_result_data) {
  AuctionResult result = MakeARandomSingleSellerAuctionResult(
      component_auction_result_data.buyer_list);
  result.set_interest_group_owner(component_auction_result_data.test_ig_owner);
  result.mutable_win_reporting_urls()
      ->mutable_component_seller_reporting_urls()
      ->set_reporting_url(
          component_auction_result_data.test_component_report_result_url);
  result.mutable_win_reporting_urls()
      ->mutable_component_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(component_auction_result_data.test_component_event,
                    component_auction_result_data
                        .test_component_interaction_reporting_url);
  result.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(
          component_auction_result_data.test_component_win_reporting_url);
  result.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(component_auction_result_data.test_component_event,
                    component_auction_result_data
                        .test_component_interaction_reporting_url);
  result.set_ad_type(AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);
  result.mutable_auction_params()->set_ciphertext_generation_id(
      component_auction_result_data.test_ig_owner);
  result.mutable_auction_params()->set_component_seller(
      component_auction_result_data.test_component_seller);
  result.mutable_ad_component_render_urls()->Add(MakeARandomString());
  result.mutable_ad_component_render_urls()->Add(MakeARandomString());
  result.set_bid(1);
  result.set_bid_currency(MakeARandomString());
  return result;
}

AuctionResult MakeARandomComponentAuctionResult(
    std::string generation_id, std::string top_level_seller,
    std::vector<std::string> buyer_list) {
  AuctionResult result =
      MakeARandomSingleSellerAuctionResult(std::move(buyer_list));
  result.set_top_level_seller(std::move(top_level_seller));
  result.set_ad_type(AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);
  result.mutable_auction_params()->set_ciphertext_generation_id(
      std::move(generation_id));
  result.mutable_auction_params()->set_component_seller(MakeARandomString());
  result.mutable_ad_component_render_urls()->Add(MakeARandomString());
  result.mutable_ad_component_render_urls()->Add(MakeARandomString());
  result.set_bid(MakeARandomNumber<float>(0.1, 1.0));
  result.set_bid_currency(MakeARandomString());
  return result;
}
}  // namespace privacy_sandbox::bidding_auction_servers
