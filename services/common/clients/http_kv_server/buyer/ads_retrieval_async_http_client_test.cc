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
constexpr char kTestRetrievalData[] = "test_private_embeddings";
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
  ABSL_LOG(INFO) << "Request is:\n" << request;
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

  // Verify partition's key groups.
  ASSERT_TRUE(partition.HasMember(kArguments));
  const auto& arguments = partition[kArguments];
  ASSERT_TRUE(arguments.IsArray());
  EXPECT_EQ(arguments.Size(), 4);

  int i = 0;
  for (const auto& argument_entry : arguments.GetArray()) {
    if (i == 3) {
      // Skipping the verification of optional ad ids.
      break;
    }

    ASSERT_TRUE(argument_entry.IsObject());
    ASSERT_TRUE(argument_entry.HasMember(kData));

    const auto& data = argument_entry[kData];

    switch (i) {
      case 0:  // Protected Signals
        ASSERT_TRUE(data.IsString());
        EXPECT_EQ(std::string(data.GetString()), kTestProtectedSignals);
        break;
      case 1:  // Device Metadata
        ASSERT_TRUE(data.IsObject());
        break;
      case 2:  // Contextual Signals
        ASSERT_TRUE(data.IsString());
        EXPECT_EQ(std::string(data.GetString()), kTestContextualSignals);
        break;
      default:
        break;
    }
    ++i;
  }
}

class KeyValueAsyncHttpClientTest : public testing::Test {
 protected:
  std::unique_ptr<MockHttpFetcherAsync> mock_http_fetcher_async_ =
      std::make_unique<MockHttpFetcherAsync>();

  // A full formed ad retrieval request input data.
  std::unique_ptr<AdRetrievalInput> ad_retrieval_input_ =
      std::make_unique<AdRetrievalInput>(
          AdRetrievalInput{.retrieval_data = kTestProtectedSignals,
                           .contextual_signals = kTestContextualSignals,
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

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
