//  Copyright 2023 Google LLC
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

#include "services/common/clients/http_kv_server/buyer/ads_retrieval_async_http_client.h"

#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "gtest/gtest-death-test.h"
#include "gtest/gtest.h"
#include "services/common/clients/http_kv_server/buyer/ad_retrieval_constants.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::AnyNumber;
using ::testing::Contains;
using ::testing::HasSubstr;

constexpr char kTestContextualSignals[] = "test_contextual_signals";
constexpr char kTestProtectedSignals[] = "test_protected_signals";
constexpr char kTestProtectedEmbeddings[] = "test_private_embeddings";
constexpr char kTestClientIp[] = "10.1.2.3";
constexpr char kTestUserAgent[] = "test_user_agent";
constexpr char kTestLanguage[] = "test_user_language";
constexpr char kTestAdRetrievalHostname[] = "xds://ad_retrieval_service";
constexpr int kTestRequestTimeout = 5000;

using AdsRetrievalOutputType =
    absl::StatusOr<std::unique_ptr<AdRetrievalOutput>>;

// Validates that the key list contains only one value and compares it against
// the provided expected value.
void MatchesSingleEntryKeyList(const rapidjson::Value& key_list,
                               absl::string_view expected_value) {
  ASSERT_EQ(key_list.GetArray().Size(), 1);
  const auto& observed_key = key_list.GetArray()[0];
  ASSERT_TRUE(observed_key.IsString());
  EXPECT_EQ(std::string(observed_key.GetString()), expected_value);
}

// Validates device metadata in the deserialized request.
void ValidateDeviceMetadata(const rapidjson::Value& key_list) {
  ASSERT_EQ(key_list.GetArray().Size(), 3);
  bool found_client_ip = false;
  bool found_user_agent = false;
  bool found_language = false;
  for (const auto& device_data : key_list.GetArray()) {
    ASSERT_TRUE(device_data.IsString());
    const auto device_data_str = std::string(device_data.GetString());
    std::vector<std::string> key_val =
        absl::StrSplit(device_data_str, kKeyValDelimiter);
    const auto& key = key_val[0];
    const auto& val = key_val[1];
    if (key == kClientIp) {
      found_client_ip = true;
      EXPECT_EQ(val, kTestClientIp);
    } else if (key == kUserAgent) {
      found_user_agent = true;
      EXPECT_EQ(val, kTestUserAgent);
    } else if (key == kAcceptLanguage) {
      found_language = true;
      EXPECT_EQ(val, kTestLanguage);
    }
  }
  EXPECT_TRUE(found_client_ip);
  EXPECT_TRUE(found_language);
  EXPECT_TRUE(found_user_agent);
}

void ValidateRequestBody(absl::string_view request) {
  auto parsed_json = ParseJsonString(request);
  ASSERT_TRUE(parsed_json.ok()) << parsed_json.status();

  EXPECT_TRUE(parsed_json->IsObject());
  auto partitions_array_it = parsed_json->FindMember(kPartitions);
  ASSERT_TRUE(partitions_array_it != parsed_json->MemberEnd());
  const auto& partitions_array = partitions_array_it->value;
  ASSERT_TRUE(partitions_array.IsArray());
  EXPECT_EQ(partitions_array.Size(), 1);

  const auto& partition = partitions_array[0];
  ASSERT_TRUE(partition.IsObject());

  // Verify partition's ID.
  ASSERT_TRUE(partition.HasMember(kId));
  const auto& partition_id = partition[kId];
  ASSERT_TRUE(partition_id.IsNumber());
  EXPECT_EQ(partition_id.GetInt(), kPartitionIdValue);

  // Verify partition's key groups.
  ASSERT_TRUE(partition.HasMember(kKeyGroups));
  const auto& key_groups = partition[kKeyGroups];
  ASSERT_TRUE(key_groups.IsArray());
  EXPECT_EQ(key_groups.Size(), 4);

  // Ensure that we find all the data in the deserialized request.
  bool found_contextual_signals = false;
  bool found_protected_signals = false;
  bool found_protected_embeddings = false;
  bool found_device_metadata = false;

  for (const auto& key_group_entry : key_groups.GetArray()) {
    ASSERT_TRUE(key_group_entry.IsObject());
    ASSERT_TRUE(key_group_entry.HasMember(kKeyList));
    ASSERT_TRUE(key_group_entry.HasMember(kTags));

    const auto& key_tags_array = key_group_entry[kTags];
    ASSERT_TRUE(key_tags_array.IsArray());
    ASSERT_EQ(key_tags_array.Size(), 1);
    const auto key_tag = std::string(key_tags_array[0].GetString());
    LOG(INFO) << "Found key tag: " << key_tag;

    const auto& key_list = key_group_entry[kKeyList];
    ASSERT_TRUE(key_list.IsArray());
    if (key_tag == kContextualSignals) {
      found_contextual_signals = true;
      MatchesSingleEntryKeyList(key_list, kTestContextualSignals);
    } else if (key_tag == kProtectedSignals) {
      found_protected_signals = true;
      MatchesSingleEntryKeyList(key_list, kTestProtectedSignals);
    } else if (key_tag == kProtectedEmbeddings) {
      found_protected_embeddings = true;
      MatchesSingleEntryKeyList(key_list, kTestProtectedEmbeddings);
    } else if (key_tag == kDeviceMetadata) {
      found_device_metadata = true;
      ValidateDeviceMetadata(key_list);
    }
  }

  EXPECT_TRUE(found_contextual_signals);
  EXPECT_TRUE(found_protected_signals);
  EXPECT_TRUE(found_protected_embeddings);
  EXPECT_TRUE(found_device_metadata);
}

class KeyValueAsyncHttpClientTest : public testing::Test {
 protected:
  std::unique_ptr<MockHttpFetcherAsync> mock_http_fetcher_async_ =
      std::make_unique<MockHttpFetcherAsync>();

  // A full formed ad retrieval request input data.
  std::unique_ptr<AdRetrievalInput> ad_retrieval_input_ =
      std::make_unique<AdRetrievalInput>(
          AdRetrievalInput{.protected_signals = kTestProtectedSignals,
                           .contextual_signals = kTestContextualSignals,
                           .protected_embeddings = kTestProtectedEmbeddings,
                           .device_metadata = {
                               .client_ip = kTestClientIp,
                               .user_agent = kTestUserAgent,
                               .accept_language = kTestLanguage,
                           }});
};

TEST_F(KeyValueAsyncHttpClientTest, CorrectHttpMethodIsCalled) {
  EXPECT_CALL(*mock_http_fetcher_async_, PutUrl).Times(AnyNumber());
  AdsRetrievalAsyncHttpClient http_client(kTestAdRetrievalHostname,
                                          std::move(mock_http_fetcher_async_));
  http_client.Execute(
      std::move(ad_retrieval_input_), {}, [](AdsRetrievalOutputType) {},
      absl::Milliseconds(kTestRequestTimeout));
}

TEST_F(KeyValueAsyncHttpClientTest, CorretUrlIsUsedInRequest) {
  EXPECT_CALL(*mock_http_fetcher_async_, PutUrl)
      .WillRepeatedly(
          [](HTTPRequest request, int timeout_ms,
             absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                 done_callback) {
            EXPECT_EQ(request.url, kTestAdRetrievalHostname);
          });
  AdsRetrievalAsyncHttpClient http_client(kTestAdRetrievalHostname,
                                          std::move(mock_http_fetcher_async_));
  http_client.Execute(
      std::move(ad_retrieval_input_), {}, [](AdsRetrievalOutputType) {},
      absl::Milliseconds(kTestRequestTimeout));
}

TEST_F(KeyValueAsyncHttpClientTest, CorrectHeaderIsProvidedInRequest) {
  EXPECT_CALL(*mock_http_fetcher_async_, PutUrl)
      .WillRepeatedly(
          [](HTTPRequest request, int timeout_ms,
             absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                 done_callback) {
            EXPECT_THAT(request.headers, Contains(kApplicationJsonHeader));
          });
  AdsRetrievalAsyncHttpClient http_client(kTestAdRetrievalHostname,
                                          std::move(mock_http_fetcher_async_));
  http_client.Execute(
      std::move(ad_retrieval_input_), {}, [](AdsRetrievalOutputType) {},
      absl::Milliseconds(kTestRequestTimeout));
}

TEST_F(KeyValueAsyncHttpClientTest, CorrectRequestBodyIsProvidedInRequest) {
  EXPECT_CALL(*mock_http_fetcher_async_, PutUrl)
      .WillOnce([](HTTPRequest request, int timeout_ms,
                   absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                       done_callback) {
        ASSERT_FALSE(request.body.empty());
        ValidateRequestBody(request.body);
      });
  AdsRetrievalAsyncHttpClient http_client(kTestAdRetrievalHostname,
                                          std::move(mock_http_fetcher_async_),
                                          /*pre_warm=*/false);
  http_client.Execute(
      std::move(ad_retrieval_input_), {}, [](AdsRetrievalOutputType) {},
      absl::Milliseconds(kTestRequestTimeout));
}

TEST_F(KeyValueAsyncHttpClientTest, CorrectTimeoutIsSet) {
  EXPECT_CALL(*mock_http_fetcher_async_, PutUrl)
      .WillOnce([](HTTPRequest request, int timeout_ms,
                   absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                       done_callback) {
        EXPECT_EQ(timeout_ms, kTestRequestTimeout);
      });
  AdsRetrievalAsyncHttpClient http_client(kTestAdRetrievalHostname,
                                          std::move(mock_http_fetcher_async_),
                                          /*pre_warm=*/false);
  http_client.Execute(
      std::move(ad_retrieval_input_), {}, [](AdsRetrievalOutputType) {},
      absl::Milliseconds(kTestRequestTimeout));
}

TEST_F(KeyValueAsyncHttpClientTest, ConnectionIsPreWarmedAppropriately) {
  EXPECT_CALL(*mock_http_fetcher_async_, PutUrl)
      .WillOnce([](HTTPRequest request, int timeout_ms,
                   absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                       done_callback) {
        EXPECT_EQ(timeout_ms, kPreWarmRequestTimeout);
      });
  AdsRetrievalAsyncHttpClient http_client(kTestAdRetrievalHostname,
                                          std::move(mock_http_fetcher_async_));
}

TEST(DeserializationTest, ExpectsRequestObject) {
  std::string response = R"JSON([])JSON";
  EXPECT_DEBUG_DEATH(AdRetrievalOutput::FromJson(response), "");
}

TEST(DeserializationTest, MissingPartitionsMeansEmptyResponse) {
  std::string response = R"JSON({})JSON";
  auto ad_retrieval_output = AdRetrievalOutput::FromJson(response);
  EXPECT_TRUE(ad_retrieval_output.ok());
  EXPECT_TRUE(ad_retrieval_output->ads.empty());
  EXPECT_TRUE(ad_retrieval_output->contextual_embeddings.empty());
}

TEST(DeserializationTest, OnlySinglePartitionIsPermitted) {
  std::string response = R"JSON({"partitions": []})JSON";
  auto ad_retrieval_output = AdRetrievalOutput::FromJson(response);
  EXPECT_FALSE(ad_retrieval_output.ok()) << ad_retrieval_output.status();
  EXPECT_THAT(ad_retrieval_output.status().message(),
              HasSubstr(kUnexpectedPartitions));
}

TEST(DeserializationTest, MissingKeyGroupOutputsMeansEmptyResponse) {
  std::string response = R"JSON({"partitions": [{"id": 0}]})JSON";
  auto ad_retrieval_output = AdRetrievalOutput::FromJson(response);
  EXPECT_TRUE(ad_retrieval_output.ok());
  EXPECT_TRUE(ad_retrieval_output->ads.empty());
  EXPECT_TRUE(ad_retrieval_output->contextual_embeddings.empty());
}

TEST(DeserializationTest, EmptyKeyGroupOutputsMeansEmptyResponse) {
  std::string response =
      R"JSON({"partitions": [{"id": 0, "keyGroupOutputs": []}]})JSON";
  auto ad_retrieval_output = AdRetrievalOutput::FromJson(response);
  EXPECT_TRUE(ad_retrieval_output.ok());
  EXPECT_TRUE(ad_retrieval_output->ads.empty());
  EXPECT_TRUE(ad_retrieval_output->contextual_embeddings.empty());
}

TEST(DeserializationTest, ExpectBothTagAndKeyValueMembers) {
  std::string response = R"JSON(
  {
    "partitions": [
        {
            "id": 0,
            "keyGroupOutputs": [
                {
                    "tags": []
                }
            ]
        }
    ]
  })JSON";
  EXPECT_DEBUG_DEATH(AdRetrievalOutput::FromJson(response), "");

  response = R"JSON(
  {
    "partitions": [
        {
            "id": 0,
            "keyGroupOutputs": [
                {
                    "keyValues": []
                }
            ]
        }
    ]
  })JSON";
  EXPECT_DEBUG_DEATH(AdRetrievalOutput::FromJson(response), "");
}

TEST(DeserializationTest, ExpectASingleTag) {
  std::string response = R"JSON(
  {
    "partitions": [
        {
            "id": 0,
            "keyGroupOutputs": [
                {
                    "tags": [],
                    "keyValues": []
                }
            ]
        }
    ]
  })JSON";
  auto ad_retrieval_output = AdRetrievalOutput::FromJson(response);
  EXPECT_FALSE(ad_retrieval_output.ok());
  EXPECT_THAT(ad_retrieval_output.status().message(),
              HasSubstr(kUnexpectedTags));
}

TEST(DeserializationTest, ExpectASingleAdsArray) {
  std::string response = R"JSON(
  {
    "partitions": [
        {
            "id": 0,
            "keyGroupOutputs": [
                {
                    "tags": [
                        "ads"
                    ],
                    "keyValues": []
                },
                {
                    "tags": [
                        "ads"
                    ],
                    "keyValues": []
                }
            ]
        }
    ]
  })JSON";
  EXPECT_DEBUG_DEATH(AdRetrievalOutput::FromJson(response), "");
}

TEST(DeserializationTest, ExpectASingleContextualEmbeddingsArray) {
  std::string response = R"JSON(
  {
    "partitions": [
        {
            "id": 0,
            "keyGroupOutputs": [
                {
                    "tags": [
                        "contextualEmbeddings"
                    ],
                    "keyValues": []
                },
                {
                    "tags": [
                        "contextualEmbeddings"
                    ],
                    "keyValues": []
                }
            ]
        }
    ]
  })JSON";
  EXPECT_DEBUG_DEATH(AdRetrievalOutput::FromJson(response), "");
}

TEST(DeserializationTest, SuccessfullyParsesAWellFormedResponse) {
  std::string response = R"JSON(
  {
    "partitions": [
        {
            "id": 0,
            "keyGroupOutputs": [
                {
                    "tags": [
                        "ads"
                    ],
                    "keyValues": {
                        "adId123": {
                            "value": {
                                "url": "https://example.com/ad123",
                                "adEmbeddings": "ZXhhbXBsZQ=="
                            }
                        },
                        "adId456": {
                            "value": {
                                "url": "https://example.com/ad456",
                                "adEmbeddings": "YW5vdGhlciBleGFtcGxl"
                            }
                        }
                    }
                },
                {
                    "tags": [
                        "contextualEmbeddings"
                    ],
                    "keyValues": {
                        "contextualEmbeddings1": {
                            "value": "Y29udGV4dHVhbCBlbWJlZGRpbmc="
                        },
                        "contextualEmbeddings2": {
                            "value": "Y29udGV4dA=="
                        }
                    }
                }
            ]
        }
    ]
  })JSON";
  auto ad_retrieval_output = AdRetrievalOutput::FromJson(response);
  ASSERT_TRUE(ad_retrieval_output.ok()) << ad_retrieval_output.status();
  // Since the structure of how ads are returned from the ads retrieval service
  // might change in near future, we only verify the presence of fields here
  // instead of the actual structure.
  EXPECT_FALSE(ad_retrieval_output->ads.empty());
  EXPECT_FALSE(ad_retrieval_output->contextual_embeddings.empty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
