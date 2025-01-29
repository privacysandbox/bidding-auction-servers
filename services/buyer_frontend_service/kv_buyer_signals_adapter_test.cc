/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "kv_buyer_signals_adapter.h"

#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "services/common/test/utils/test_utils.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using google::protobuf::TextFormat;
using google::scp::core::test::EqualsProto;
using privacy_sandbox::bidding_auction_servers::BiddingSignalsRequest;
using privacy_sandbox::bidding_auction_servers::BuyerInput;
using privacy_sandbox::bidding_auction_servers::ConvertV2BiddingSignalsToV1;
using privacy_sandbox::bidding_auction_servers::CreateV2BiddingRequest;
using privacy_sandbox::bidding_auction_servers::GetBidsRequest;

TEST(KvBuyerSignalsAdapter, Convert) {
  kv_server::v2::GetValuesResponse response;
  std::string compression_group = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "nohello": {
              "value": "world"
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
            "keys"
          ],
          "keyValues": {
            "hello2": {
              "value": "world2"
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
                      absl::CEscape(RemoveWhiteSpaces(compression_group))),
      &response));
  auto result = ConvertV2BiddingSignalsToV1(
      std::make_unique<kv_server::v2::GetValuesResponse>(response));
  CHECK_OK(result) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "keys": {
          "hello": {
            "value": "world"
          },
          "hello2": {
            "value": "world2"
          }
      }
    })json";
  ASSERT_EQ(*((*result)->trusted_signals),
            RemoveWhiteSpaces(expected_parsed_signals));
}

TEST(KvBuyerSignalsAdapter, MultipleCompressionGroups) {
  kv_server::v2::GetValuesResponse response;
  std::string compression_group = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "nohello": {
              "value": "world"
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
            "keys"
          ],
          "keyValues": {
            "hello2": {
              "value": "world2"
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
            "keys"
          ],
          "keyValues": {
            "hello44": {
              "value": "world44"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "blah": {
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
            "keys"
          ],
          "keyValues": {
            "hello24": {
              "value": "world24"
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
                      absl::CEscape(RemoveWhiteSpaces(compression_group)),
                      absl::CEscape(RemoveWhiteSpaces(compression_group_2))),
      &response));
  auto result = ConvertV2BiddingSignalsToV1(
      std::make_unique<kv_server::v2::GetValuesResponse>(response));
  CHECK_OK(result) << result.status();
  std::string expected_parsed_signals =
      R"json(
      {
        "keys": {
          "hello": {
            "value": "world"
          },
          "hello2": {
            "value": "world2"
          },
          "hello44": {
            "value": "world44"
          },
          "hello24": {
            "value": "world24"
          }
      }
    })json";
  ASSERT_EQ(*((*result)->trusted_signals),
            RemoveWhiteSpaces(expected_parsed_signals));
}

TEST(KvBuyerSignalsAdapter, MalformedJson) {
  kv_server::v2::GetValuesResponse response;
  std::string compression_group = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputsFAIL": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "hello": {
              "value": "world"
            }
          }
        },
        {
          "tags": [
            "structured",
            "groupNames"
          ],
          "keyValues": {
            "nohello": {
              "value": "world"
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
            "keys"
          ],
          "keyValues": {
            "hello2": {
              "value": "world2"
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
                      absl::CEscape(RemoveWhiteSpaces(compression_group))),
      &response));
  auto result = ConvertV2BiddingSignalsToV1(
      std::make_unique<kv_server::v2::GetValuesResponse>(response));
  ASSERT_FALSE(result.ok());
}

TEST(KvBuyerSignalsAdapter, EmptyJson) {
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"(
        compression_groups {
          compression_group_id : 33
          content : ""
        })",
      &response));
  auto result = ConvertV2BiddingSignalsToV1(
      std::make_unique<kv_server::v2::GetValuesResponse>(response));
  ASSERT_FALSE(result.ok());
}

BuyerInput::InterestGroup MakeAnInterestGroup(const std::string& id,
                                              int keys_number) {
  BuyerInput::InterestGroup interest_group;
  interest_group.set_name(absl::StrCat("ig_name_", id));
  for (int i = 0; i < keys_number; i++) {
    interest_group.mutable_bidding_signals_keys()->Add(
        absl::StrCat("bidding_signal_key_", id, i));
  }
  return interest_group;
}

TEST(KvBuyerSignalsAdapter, CreateV2BiddingRequestSuccess) {
  kv_server::v2::GetValuesRequest expected;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        client_version: "Bna.PA.Buyer.20240930"
        metadata {
          fields {
            key: "client_type"
            value { string_value: "1" }
          }
          fields {
            key: "experiment_group_id"
            value { string_value: "1689" }
          }
          fields {
            key: "hostname"
            value { string_value: "somepublisher.com" }
          }
        }
        partitions {
          id: 0
          compression_group_id: 0
          arguments {
            tags { values { string_value: "keys" } }
            data {
              list_value { values { string_value: "bidding_signal_key_00" } }
            }
          }
          arguments {
            tags { values { string_value: "keys" } }
            data {
              list_value { values { string_value: "bidding_signal_key_01" } }
            }
          }
        }
        partitions {
          id: 1
          compression_group_id: 1
          arguments {
            tags { values { string_value: "keys" } }
            data {
              list_value { values { string_value: "bidding_signal_key_10" } }
            }
          }
          arguments {
            tags { values { string_value: "keys" } }
            data {
              list_value { values { string_value: "bidding_signal_key_11" } }
            }
          }
        }
        log_context {
          generation_id: "generation_id"
          adtech_debug_id: "debug_id"
        }
        consented_debug_config { is_consented: true token: "test_token" })pb",
      &expected));
  privacy_sandbox::server_common::ConsentedDebugConfiguration
      consented_debug_configuration;
  consented_debug_configuration.set_is_consented(true);
  consented_debug_configuration.set_token("test_token");
  privacy_sandbox::server_common::LogContext log_context;
  log_context.set_generation_id("generation_id");
  log_context.set_adtech_debug_id("debug_id");
  GetBidsRequest::GetBidsRawRequest bids_request;
  bids_request.set_buyer_kv_experiment_group_id(1689);
  bids_request.set_client_type(
      privacy_sandbox::bidding_auction_servers::CLIENT_TYPE_ANDROID);
  bids_request.set_publisher_name("somepublisher.com");
  *bids_request.mutable_consented_debug_config() =
      std::move(consented_debug_configuration);
  *bids_request.mutable_log_context() = std::move(log_context);
  for (int i = 0; i < 2; i++) {
    *bids_request.mutable_buyer_input()->mutable_interest_groups()->Add() =
        MakeAnInterestGroup(std::to_string(i), 2);
  }
  BiddingSignalsRequest bidding_signals_request(bids_request, {});
  auto maybe_result = CreateV2BiddingRequest(bidding_signals_request);
  ASSERT_TRUE(maybe_result.ok()) << maybe_result.status();
  auto& request = *(*maybe_result);
  EXPECT_THAT(request, EqualsProto(expected));
}

TEST(KvBuyerSignalsAdapter, CreateV2BiddingRequestCreationNoIGsFail) {
  privacy_sandbox::server_common::ConsentedDebugConfiguration
      consented_debug_configuration;
  consented_debug_configuration.set_is_consented(true);
  consented_debug_configuration.set_token("test_token");
  privacy_sandbox::server_common::LogContext log_context;
  log_context.set_generation_id("generation_id");
  log_context.set_adtech_debug_id("debug_id");
  GetBidsRequest::GetBidsRawRequest bids_request;
  bids_request.set_buyer_kv_experiment_group_id(1689);
  bids_request.set_client_type(
      privacy_sandbox::bidding_auction_servers::CLIENT_TYPE_ANDROID);
  bids_request.set_publisher_name("somepublisher.com");
  *bids_request.mutable_consented_debug_config() =
      std::move(consented_debug_configuration);
  *bids_request.mutable_log_context() = std::move(log_context);

  BiddingSignalsRequest bidding_signals_request(bids_request, {});
  auto maybe_result = CreateV2BiddingRequest(bidding_signals_request);
  ASSERT_FALSE(maybe_result.ok());
}

TEST(KvBuyerSignalsAdapter,
     CreateV2BiddingRequestWithContextualBuyerSignalsSuccess) {
  kv_server::v2::GetValuesRequest expected;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        client_version: "Bna.PA.Buyer.20240930"
        metadata {
          fields {
            key: "buyer_signals"
            value { string_value: "contextual_buyer_signals" }
          }
          fields {
            key: "client_type"
            value { string_value: "1" }
          }
          fields {
            key: "experiment_group_id"
            value { string_value: "1689" }
          }
          fields {
            key: "hostname"
            value { string_value: "somepublisher.com" }
          }
        }
        partitions {
          id: 0
          compression_group_id: 0
          arguments {
            tags { values { string_value: "keys" } }
            data {
              list_value { values { string_value: "bidding_signal_key_00" } }
            }
          }
          arguments {
            tags { values { string_value: "keys" } }
            data {
              list_value { values { string_value: "bidding_signal_key_01" } }
            }
          }
        }
        partitions {
          id: 1
          compression_group_id: 1
          arguments {
            tags { values { string_value: "keys" } }
            data {
              list_value { values { string_value: "bidding_signal_key_10" } }
            }
          }
          arguments {
            tags { values { string_value: "keys" } }
            data {
              list_value { values { string_value: "bidding_signal_key_11" } }
            }
          }
        }
        log_context {
          generation_id: "generation_id"
          adtech_debug_id: "debug_id"
        }
        consented_debug_config { is_consented: true token: "test_token" })pb",
      &expected));
  privacy_sandbox::server_common::ConsentedDebugConfiguration
      consented_debug_configuration;
  consented_debug_configuration.set_is_consented(true);
  consented_debug_configuration.set_token("test_token");
  privacy_sandbox::server_common::LogContext log_context;
  log_context.set_generation_id("generation_id");
  log_context.set_adtech_debug_id("debug_id");
  GetBidsRequest::GetBidsRawRequest bids_request;
  bids_request.set_buyer_kv_experiment_group_id(1689);
  bids_request.set_client_type(
      privacy_sandbox::bidding_auction_servers::CLIENT_TYPE_ANDROID);
  bids_request.set_publisher_name("somepublisher.com");
  bids_request.set_buyer_signals("contextual_buyer_signals");
  *bids_request.mutable_consented_debug_config() =
      std::move(consented_debug_configuration);
  *bids_request.mutable_log_context() = std::move(log_context);
  for (int i = 0; i < 2; i++) {
    *bids_request.mutable_buyer_input()->mutable_interest_groups()->Add() =
        MakeAnInterestGroup(std::to_string(i), 2);
  }
  BiddingSignalsRequest bidding_signals_request(bids_request, {});
  auto maybe_result =
      CreateV2BiddingRequest(bidding_signals_request,
                             /* propagate_buyer_signals_to_tkv */ true);
  ASSERT_TRUE(maybe_result.ok()) << maybe_result.status();
  auto& request = *(*maybe_result);
  EXPECT_THAT(request, EqualsProto(expected));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
