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

#include "kv_seller_signals_adapter.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "services/common/util/json_util.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using google::protobuf::TextFormat;
using google::scp::core::test::EqualsProto;
using ::testing::UnorderedElementsAre;

kv_server::v2::RequestPartition GetPartitionWithoutId(
    const kv_server::v2::GetValuesRequest& request, int id) {
  kv_server::v2::RequestPartition partition;
  for (const auto& reqpartition : request.partitions()) {
    if (reqpartition.id() == id) {
      partition.mutable_arguments()->Add(reqpartition.arguments().begin(),
                                         reqpartition.arguments().end());
    }
  }
  return partition;
}

class KvSellerSignalsAdapterTest : public ::testing::Test {
 protected:
  KVV2AdapterStats v2_adapter_stats_;
};

TEST_F(KvSellerSignalsAdapterTest, CreateV2ScoringRequestSuccess) {
  google::protobuf::Struct expected_metadata;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        fields {
          key: "client_type"
          value { string_value: "0" }
        }
        fields {
          key: "experiment_group_id"
          value { string_value: "1787" }
        }
      )pb",
      &expected_metadata));
  kv_server::v2::RequestPartition partition1;
  kv_server::v2::RequestPartition partition2;
  kv_server::v2::RequestPartition partition3;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        arguments {
          tags { values { string_value: "renderUrls" } }
          data {
            list_value {
              values { string_value: "https://adTech.com/ad?id=somead" }
            }
          }
        }
        arguments {
          tags { values { string_value: "adComponentRenderUrls" } }
          data {
            list_value {
              values {
                string_value: "https://adTech.com/adComponent?id=someadcomponent"
              }
            }
          }
        }
      )pb",
      &partition1));
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        arguments {
          tags { values { string_value: "renderUrls" } }
          data {
            list_value {
              values { string_value: "https://adTech.com/ad?id=someotherad" }
            }
          }
        }
        arguments {
          tags { values { string_value: "adComponentRenderUrls" } }
          data {
            list_value {
              values {
                string_value: "https://adTech.com/adComponent?id=someotheradcomponent"
              }
            }
          }
        }
      )pb",
      &partition2));
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        arguments {
          tags { values { string_value: "renderUrls" } }
          data {
            list_value {
              values { string_value: "https://adTech.com/ad?id=anotherad" }
            }
          }
        }
        arguments {
          tags { values { string_value: "adComponentRenderUrls" } }
          data {
            list_value {
              values {
                string_value: "https://adTech.com/adComponent?id=anotheradcomponent"
              }
            }
          }
        }
      )pb",
      &partition3));

  auto get_bid_response1 =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  auto* bid1 = get_bid_response1->mutable_bids()->Add();
  bid1->set_render("https://adTech.com/ad?id=somead");
  bid1->add_ad_components("https://adTech.com/adComponent?id=someadcomponent");
  auto* bid2 = get_bid_response1->mutable_bids()->Add();
  bid2->set_render("https://adTech.com/ad?id=someotherad");
  bid2->add_ad_components(
      "https://adTech.com/adComponent?id=someotheradcomponent");

  auto get_bid_response2 =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  auto* bid3 = get_bid_response2->mutable_bids()->Add();
  bid3->set_render("https://adTech.com/ad?id=anotherad");
  bid3->add_ad_components(
      "https://adTech.com/adComponent?id=anotheradcomponent");

  BuyerBidsResponseMap buyer_bids_map;
  buyer_bids_map.try_emplace("igowner1.com", std::move(get_bid_response1));
  buyer_bids_map.try_emplace("igowner2.com", std::move(get_bid_response2));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, /*filtering_metadata=*/{},
                            ClientType::CLIENT_TYPE_UNKNOWN,
                            /*seller_kv_experiment_group_id=*/"1787");
  auto maybe_result = CreateV2ScoringRequest(scoring_signals_request,
                                             /*is_pas_enabled=*/false);
  ASSERT_TRUE(maybe_result.ok()) << maybe_result.status();
  auto& request = *(*maybe_result);

  EXPECT_EQ(request.client_version(), kSellerKvClientVersion);
  EXPECT_THAT(request.metadata(), EqualsProto(expected_metadata));
  EXPECT_EQ(request.partitions().size(), 3);
  EXPECT_THAT(std::vector({GetPartitionWithoutId(request, 0).DebugString(),
                           GetPartitionWithoutId(request, 1).DebugString(),
                           GetPartitionWithoutId(request, 2).DebugString()}),
              testing::UnorderedElementsAre(partition1.DebugString(),
                                            partition2.DebugString(),
                                            partition3.DebugString()));
}

TEST_F(KvSellerSignalsAdapterTest,
       CreateV2ScoringRequestWithConsentedDebugConfigSuccess) {
  server_common::ConsentedDebugConfiguration expected_consented_debug_config;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(is_consented: true
                                               token: "test_token")pb",
                                          &expected_consented_debug_config));
  kv_server::v2::RequestPartition partition;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        arguments {
          tags { values { string_value: "renderUrls" } }
          data {
            list_value {
              values { string_value: "https://adTech.com/ad?id=somead" }
            }
          }
        }
      )pb",
      &partition));
  auto get_bid_response =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  auto* bid = get_bid_response->mutable_bids()->Add();
  bid->set_render("https://adTech.com/ad?id=somead");

  BuyerBidsResponseMap buyer_bids_map;
  buyer_bids_map.try_emplace("igowner1.com", std::move(get_bid_response));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, /*filtering_metadata=*/{},
                            ClientType::CLIENT_TYPE_UNKNOWN,
                            /*seller_kv_experiment_group_id=*/"");
  auto maybe_result = CreateV2ScoringRequest(scoring_signals_request,
                                             /*is_pas_enabled=*/false,
                                             expected_consented_debug_config);
  ASSERT_TRUE(maybe_result.ok()) << maybe_result.status();
  auto& request = *(*maybe_result);

  EXPECT_EQ(request.client_version(), kSellerKvClientVersion);
  EXPECT_THAT(request.consented_debug_config(),
              EqualsProto(expected_consented_debug_config));
  EXPECT_EQ(request.partitions().size(), 1);
  EXPECT_THAT(GetPartitionWithoutId(request, 0), EqualsProto(partition));
}

TEST_F(KvSellerSignalsAdapterTest,
       CreateV2ScoringRequestSetsPasEnabledSuccess) {
  google::protobuf::Struct expected_metadata;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        fields {
          key: "client_type"
          value { string_value: "0" }
        }
        fields {
          key: "experiment_group_id"
          value { string_value: "1787" }
        }
      )pb",
      &expected_metadata));
  kv_server::v2::RequestPartition partition1;
  kv_server::v2::RequestPartition partition2;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        arguments {
          tags { values { string_value: "renderUrls" } }
          data {
            list_value {
              values { string_value: "https://adTech.com/ad?id=somead" }
            }
          }
        }
        arguments {
          tags { values { string_value: "adComponentRenderUrls" } }
          data {
            list_value {
              values {
                string_value: "https://adTech.com/adComponent?id=someadcomponent"
              }
            }
          }
        }
      )pb",
      &partition1));
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        arguments {
          tags { values { string_value: "renderUrls" } }
          data {
            list_value {
              values { string_value: "https://adTech.com/pas?id=somepasad" }
            }
          }
        }
      )pb",
      &partition2));

  auto get_bid_response =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  auto* bid = get_bid_response->mutable_bids()->Add();
  bid->set_render("https://adTech.com/ad?id=somead");
  bid->add_ad_components("https://adTech.com/adComponent?id=someadcomponent");
  get_bid_response->mutable_protected_app_signals_bids()->Add()->set_render(
      "https://adTech.com/pas?id=somepasad");

  BuyerBidsResponseMap buyer_bids_map;
  buyer_bids_map.try_emplace("igowner.com", std::move(get_bid_response));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, /*filtering_metadata=*/{},
                            ClientType::CLIENT_TYPE_UNKNOWN,
                            /*seller_kv_experiment_group_id=*/"1787");
  auto maybe_result = CreateV2ScoringRequest(scoring_signals_request,
                                             /*is_pas_enabled=*/true);
  ASSERT_TRUE(maybe_result.ok()) << maybe_result.status();
  auto& request = *(*maybe_result);

  EXPECT_EQ(request.client_version(), kSellerKvClientVersion);
  EXPECT_THAT(request.metadata(), EqualsProto(expected_metadata));
  EXPECT_EQ(request.partitions().size(), 2);
  EXPECT_THAT(std::vector({GetPartitionWithoutId(request, 0).DebugString(),
                           GetPartitionWithoutId(request, 1).DebugString()}),
              testing::UnorderedElementsAre(partition1.DebugString(),
                                            partition2.DebugString()));
}

// This is for permitting graceful failures in case someone disobeys the spec,
// as AdRenderUrls are required to be present for all AdWithBids.
TEST_F(KvSellerSignalsAdapterTest, CreateV2ScoringRequestNoRenderUrlsSuccess) {
  kv_server::v2::GetValuesRequest expected;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        client_version: "Bna.PA.Seller.20240930"
        metadata {
          fields {
            key: "client_type"
            value { string_value: "0" }
          }
          fields {
            key: "experiment_group_id"
            value { string_value: "1787" }
          }
        }
        partitions {
          id: 0
          compression_group_id: 0
          arguments {
            tags { values { string_value: "adComponentRenderUrls" } }
            data {
              list_value {
                values {
                  string_value: "https://adTech.com/adComponent?id=someadcomponent"
                }
              }
            }
          }
        }
      )pb",
      &expected));

  auto get_bid_response =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  auto* bid = get_bid_response->mutable_bids()->Add();
  bid->add_ad_components("https://adTech.com/adComponent?id=someadcomponent");

  BuyerBidsResponseMap buyer_bids_map;
  buyer_bids_map.try_emplace("igowner.com", std::move(get_bid_response));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, /*filtering_metadata=*/{},
                            ClientType::CLIENT_TYPE_UNKNOWN,
                            /*seller_kv_experiment_group_id=*/"1787");
  auto maybe_result = CreateV2ScoringRequest(scoring_signals_request,
                                             /*is_pas_enabled=*/false);
  ASSERT_TRUE(maybe_result.ok()) << maybe_result.status();
  auto& request = *(*maybe_result);
  EXPECT_THAT(request, EqualsProto(expected));
}

TEST_F(KvSellerSignalsAdapterTest,
       CreateV2ScoringRequestNoAdComponentRenderUrlsSuccess) {
  kv_server::v2::GetValuesRequest expected;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        client_version: "Bna.PA.Seller.20240930"
        metadata {
          fields {
            key: "client_type"
            value { string_value: "0" }
          }
          fields {
            key: "experiment_group_id"
            value { string_value: "1787" }
          }
        }
        partitions {
          id: 0
          compression_group_id: 0
          arguments {
            tags { values { string_value: "renderUrls" } }
            data {
              list_value {
                values { string_value: "https://adTech.com/ad?id=somead" }
              }
            }
          }
        }
      )pb",
      &expected));

  auto get_bid_response =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  auto* bid1 = get_bid_response->mutable_bids()->Add();
  bid1->set_render("https://adTech.com/ad?id=somead");

  BuyerBidsResponseMap buyer_bids_map;
  buyer_bids_map.try_emplace("igowner.com", std::move(get_bid_response));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, /*filtering_metadata=*/{},
                            ClientType::CLIENT_TYPE_UNKNOWN,
                            /*seller_kv_experiment_group_id=*/"1787");
  auto maybe_result = CreateV2ScoringRequest(scoring_signals_request,
                                             /*is_pas_enabled=*/false);
  ASSERT_TRUE(maybe_result.ok()) << maybe_result.status();
  auto& request = *(*maybe_result);
  EXPECT_THAT(request, EqualsProto(expected));
}

TEST_F(KvSellerSignalsAdapterTest, CreateV2ScoringRequestNoUrlsFailure) {
  auto get_bid_response1 =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  get_bid_response1->mutable_bids()->Add();
  auto get_bid_response2 =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();

  BuyerBidsResponseMap buyer_bids_map;
  buyer_bids_map.try_emplace("igowner1.com", std::move(get_bid_response1));
  buyer_bids_map.try_emplace("igowner2.com", std::move(get_bid_response2));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, /*filtering_metadata=*/{},
                            ClientType::CLIENT_TYPE_UNKNOWN,
                            /*seller_kv_experiment_group_id=*/"1787");
  auto maybe_result = CreateV2ScoringRequest(scoring_signals_request,
                                             /*is_pas_enabled=*/false);
  ASSERT_FALSE(maybe_result.ok());
}

TEST_F(KvSellerSignalsAdapterTest, ConvertV2ResponseToV1ScoringSignalsSuccess) {
  kv_server::v2::GetValuesResponse response;
  std::string compression_group = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "renderUrls"
          ],
          "keyValues": {
            "www.exampleRenderUrl.com/ad1": {
              "value": "scoringSignalForAd1"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "filteredOutBasedOnTag": {
              "value": "blah"
            }
          }
        }
      ]
    },
    {
      "id": 1,
      "keyGroupOutputs": [
        {
          "tags": [
            "adComponentRenderUrls"
          ],
          "keyValues": {
            "www.exampleRenderUrl.com/adComponent1": {
              "value": "scoringSignalForAdComponent1"
            }
          }
        }
      ]
    }
  ])JSON";

  std::string compression_group_2 = R"JSON(
  [
    {
      "id": 3,
      "keyGroupOutputs": [
        {
          "tags": [
            "renderUrls"
          ],
          "keyValues": {
            "www.exampleRenderUrl.com/ad2": {
              "value": "scoringSignalForAd2"
            }
          }
        },
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "filteredOutBasedOnTag": {
              "value": "blah"
            }
          }
        }
      ]
    },
    {
      "id": 4,
      "keyGroupOutputs": [
        {
          "tags": [
            "adComponentRenderUrls"
          ],
          "keyValues": {
            "www.exampleRenderUrl.com/adComponent2": {
              "value": "scoringSignalForAdComponent2"
            }
          }
        }
      ]
    }
  ])JSON";

  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        }
        compression_groups {
          compression_group_id : 34
          content : "%s"
        }
        )",
                      absl::CEscape(compression_group),
                      absl::CEscape(compression_group_2)),
      &response));
  auto result = ConvertV2ResponseToV1ScoringSignals(
      std::make_unique<kv_server::v2::GetValuesResponse>(response),
      v2_adapter_stats_);
  CHECK_OK(result) << result.status();
  std::string expected_parsed_signals =
      R"JSON(
      {
        "renderUrls": {
          "www.exampleRenderUrl.com/ad1": "scoringSignalForAd1",
          "www.exampleRenderUrl.com/ad2": "scoringSignalForAd2"
        },
        "adComponentRenderUrls": {
          "www.exampleRenderUrl.com/adComponent1": "scoringSignalForAdComponent1",
          "www.exampleRenderUrl.com/adComponent2": "scoringSignalForAdComponent2"
        }
      })JSON";
  auto actual_signals_json = ParseJsonString(*((*result)->scoring_signals));
  auto expected_signals_json = ParseJsonString(expected_parsed_signals);
  ASSERT_TRUE(actual_signals_json.ok());
  ASSERT_TRUE(expected_signals_json.ok());
  ASSERT_EQ(actual_signals_json, expected_signals_json);
}

TEST_F(KvSellerSignalsAdapterTest, MalformedJson) {
  kv_server::v2::GetValuesResponse response;
  std::string compression_group = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputsFAIL": [
        {
          "tags": [
            "renderUrls"
          ],
          "keyValues": {
            "www.exampleRenderUrl.com/ad1": {
              "value": "shouldBeMalformedJson"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "filteredOutBasedOnTag": {
              "value": "blah"
            }
          }
        }
      ]
    },
    {
      "id": 1,
      "keyGroupOutputs": [
        {
          "tags": [
            "renderUrls"
          ],
          "keyValues": {
            "www.exampleRenderUrl.com/ad2": {
              "value": "shouldBeMalformedJson"
            }
          }
        }
      ]
    }
  ])JSON";
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(R"(
        compression_groups {
          compression_group_id : 33
          content : "%s"
        })",
                      absl::CEscape(compression_group)),
      &response));
  auto result = ConvertV2ResponseToV1ScoringSignals(
      std::make_unique<kv_server::v2::GetValuesResponse>(response),
      v2_adapter_stats_);
  ASSERT_FALSE(result.ok());
}

TEST_F(KvSellerSignalsAdapterTest, EmptyJson) {
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"(
        compression_groups {
          compression_group_id : 33
          content : ""
        })",
      &response));
  auto result = ConvertV2ResponseToV1ScoringSignals(
      std::make_unique<kv_server::v2::GetValuesResponse>(response),
      v2_adapter_stats_);
  ASSERT_FALSE(result.ok());
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
