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

#include "services/common/clients/http_kv_server/buyer/buyer_key_value_async_http_client.h"

#include <algorithm>

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kEgId[] = "1776";

class KeyValueAsyncHttpClientTest : public testing::Test {
 public:
  static constexpr char hostname_[] =
      "https://googleads.g.doubleclick.net/td/bts";
  std::unique_ptr<MockHttpFetcherAsync> mock_http_fetcher_async_ =
      std::make_unique<MockHttpFetcherAsync>();
  const absl::flat_hash_set<std::string> expected_urls_1 = {
      absl::StrCat(hostname_, "?keys=birkenhead,lloyd_george,clementine"),
      absl::StrCat(hostname_, "?keys=birkenhead,clementine,lloyd_george"),
      absl::StrCat(hostname_, "?keys=lloyd_george,birkenhead,clementine"),
      absl::StrCat(hostname_, "?keys=clementine,birkenhead,lloyd_george"),
      absl::StrCat(hostname_, "?keys=lloyd_george,clementine,birkenhead"),
      absl::StrCat(hostname_, "?keys=clementine,lloyd_george,birkenhead")};

 protected:
  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetBuyerValuesInput> keys) {
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
        do_nothing_done_callback;
    // Create the client
    BuyerKeyValueAsyncHttpClient buyer_key_value_async_http_client(
        hostname_, std::move(mock_http_fetcher_async_));
    auto status = buyer_key_value_async_http_client.Execute(
        std::move(keys), {}, std::move(do_nothing_done_callback),
        absl::Milliseconds(5000));
    CHECK_OK(status);
  }

  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetBuyerValuesInput> keys,
      const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
          value_checking_done_callback) {
    // Create the client
    BuyerKeyValueAsyncHttpClient buyer_key_value_async_http_client(
        hostname_, std::move(mock_http_fetcher_async_));
    auto status = buyer_key_value_async_http_client.Execute(
        std::move(keys), metadata, std::move(value_checking_done_callback),
        absl::Milliseconds(5000));
    CHECK_OK(status);
  }
};

/**
 * What does this do?
 * 1. Define inputs
 * 2. Create the actual client and actually call it
 * 3. Actual client makes a url
 * 4. Actual client calls FetchUrl with that url it made
 * 5. Since FetchUrl is a mock, we get to assert that it is called with the url
 * we expect (or in other words: assert that the actual URL the client created
 * matches the expected URL we wrote)
 * 6. If and when all of that goes well: Since the HttpAsyncFetcher is a mock,
 * we define its behavior
 * 7. We specify that it returns a string like a KV server would
 * 8. Then we pass that string into a callback exactly as the real FetchUrl
 * would have done
 * 9. THAT callback, into which the output of our mock FetchUrl is passed, is
 * the one actually defined to be the actual callback made in the actual client.
 * 10. It is THAT callback that WE define HERE in this test.
 *      Specifically we define that it shall check that the actual output
 * matches an expected output And we define that expected output here.
 */
TEST_F(KeyValueAsyncHttpClientTest,
       MakesDSPUrlCorrectlyBasicInputsAndHasCorrectOutput) {
  // Our client will be given this input object.
  const GetBuyerValuesInput getValuesClientInput = {
      {"1j1043317685", "1j112014758"},
      {"ig_name_likes_boots"},
      "www.usatoday.com",
      ClientType::CLIENT_TYPE_UNKNOWN,
      kEgId};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  // This is the URL we expect to see built from the input object.
  absl::flat_hash_set<std::string> expected_urls;
  expected_urls.emplace(absl::StrCat(
      hostname_, "?hostname=www.usatoday.com&experimentGroupId=", kEgId,
      "&keys="
      "1j1043317685,1j112014758&"
      "interestGroupNames=ig_name_likes_boots"));
  expected_urls.emplace(absl::StrCat(
      hostname_, "?hostname=www.usatoday.com&experimentGroupId=", kEgId,
      "&keys="
      "1j112014758,1j1043317685&"
      "interestGroupNames=ig_name_likes_boots"));
  // Now we define what we expect to get back out of the client, which is a
  // GetBuyerValuesOutput struct.
  const std::string expected_result = R"json({
            "keys": {
              "1j1043317685": {
                "constitution_author": "madison",
                "money_man": "hamilton"
              },
              "1j112014758": {
                "second_president": "adams"
              }
            },
            "perInterestGroupData": {
              "ig_name_likes_boots": {
                "priorityVector": {
                  "signal1": 1776
                }
              }
            }
          })json";

  std::unique_ptr<GetBuyerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetBuyerValuesOutput>(
          GetBuyerValuesOutput({expected_result}));

  // Define the lambda function which is the callback.
  // Inside this callback, we will actually check that the client correctly
  // parses what it gets back from the "server" (mocked below).
  absl::Notification callback_invoked;
  absl::AnyInvocable<void(
      absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>)&&>
      done_callback_to_check_val =
          // Capture the expected output struct for comparison
      [&callback_invoked,
       expectedOutputStructUPtr = std::move(expectedOutputStructUPtr)](
          // This is what the client actually passes back
          absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>
              actualOutputStruct) {
        ASSERT_TRUE(actualOutputStruct.ok());
        ASSERT_EQ(actualOutputStruct.value()->result,
                  expectedOutputStructUPtr->result);
        callback_invoked.Notify();
      };

  // Assert that the mocked fetcher will have the method FetchUrl called on it,
  // with the URL being expectedUrl.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      // If and when that happens: DEFINE that the FetchUrl function SHALL do
      // the following:
      //  (This part is NOT an assertion of expected behavior but rather a mock
      //  defining what it shall be)
      .WillOnce([actual_result = expected_result, &expected_urls](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_TRUE(expected_urls.contains(request.url));
        // Pack said string into a statusOr
        absl::StatusOr<std::string> resp =
            absl::StatusOr<std::string>(actual_result);
        // Now, call the callback (Note: we defined it above!) with the
        // 'response' from the 'server'
        std::move(done_callback)(resp);
      });

  // Finally, actually call the function to perform the test
  CheckGetValuesFromKeysViaHttpClient(std::move(input), {},
                                      std::move(done_callback_to_check_val));
  callback_invoked.WaitForNotification();
}

TEST_F(KeyValueAsyncHttpClientTest,
       MakesDSPUrlCorrectlyBasicInputsAndFailsForWrongOutput) {
  // Our client will be given this input object.
  const GetBuyerValuesInput getValuesClientInput = {
      {"1j386134098", "1s8yAqUg!2sZQakmQ!3sAFmfCp-n8sq_"},
      {"ig_name_likes_boots", "ig_name_ohio_state_fan"},
      "www.usatoday.com",
      ClientType::CLIENT_TYPE_UNKNOWN,
      kEgId};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  // This is the URL we expect to see built from the input object.
  absl::flat_hash_set<std::string> expected_urls = {
      absl::StrCat(
          hostname_, "?hostname=www.usatoday.com&experimentGroupId=", kEgId,
          "&keys=1j386134098,"
          "1s8yAqUg%212sZQakmQ%"
          "213sAFmfCp-n8sq_&"
          "interestGroupNames=ig_name_likes_boots,ig_name_ohio_state_fan"),
      absl::StrCat(
          hostname_, "?hostname=www.usatoday.com&experimentGroupId=", kEgId,
          "&keys=1s8yAqUg%"
          "212sZQakmQ%213sAFmfCp-n8sq_"
          ",1j386134098&"
          "interestGroupNames=ig_name_likes_boots,ig_name_ohio_state_fan"),
      absl::StrCat(
          hostname_, "?hostname=www.usatoday.com&experimentGroupId=", kEgId,
          "&keys=1j386134098,"
          "1s8yAqUg%212sZQakmQ%"
          "213sAFmfCp-n8sq_&"
          "interestGroupNames=ig_name_ohio_state_fan,ig_name_likes_boots"),
      absl::StrCat(
          hostname_, "?hostname=www.usatoday.com&experimentGroupId=", kEgId,
          "&keys=1s8yAqUg%"
          "212sZQakmQ%213sAFmfCp-n8sq_"
          ",1j386134098&"
          "interestGroupNames=ig_name_ohio_state_fan,ig_name_likes_boots")};
  // Now we define what we expect to get back out of the client, which is a
  // GetBuyerValuesOutput struct.
  // Note that this test is trivial; we use the same string
  const std::string expectedResult = R"json({
            "keys": {
              "1j386134098": {
                "constitution_author": "madison",
                "money_man": "hamilton"
              },
              "1s8yAqUg%212sZQakmQ%213sAFmfCp-n8sq_": {
                "second_president": "adams"
              }
            },
            "perInterestGroupData": {
              "ig_name_likes_boots": {
                "priorityVector": {
                  "signal1": 1776
                }
              },
              "ig_name_ohio_state_fan": {
                "priorityVector": {
                  "signal1": 1870
                }
              }
            }
          })json";

  const std::string actualResult = R"json({
            "keys": {
              "1j386134098": {
                "constitution_author": "Edmund Burke",
                "money_man": "Adam Smith"
              },
              "1s8yAqUg%212sZQakmQ%213sAFmfCp-n8sq_": {
                "second_president": "Sir Henry Pelham"
              }
            },
            "perInterestGroupData": {
              "ig_name_likes_boots": {
                "priorityVector": {
                  "signal1": -3
                }
              },
              "ig_name_ohio_state_fan": {
                "priorityVector": {
                  "signal1": -1
                }
              }
            }
          })json";
  std::unique_ptr<GetBuyerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetBuyerValuesOutput>(
          GetBuyerValuesOutput({expectedResult}));

  absl::Notification callback_invoked;
  // Define the lambda function which is the callback.
  // Inside this callback, we will actually check that the client correctly
  // parses what it gets back from the "server" (mocked below).
  absl::AnyInvocable<void(
      absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>)&&>
      done_callback_to_check_val =
          // Capture the expected output struct for comparison
      [&callback_invoked,
       expectedOutputStructUPtr = std::move(expectedOutputStructUPtr)](
          // This is what the client actually passes back
          absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>
              actualOutputStruct) {
        ASSERT_TRUE(actualOutputStruct.ok());
        ASSERT_FALSE(actualOutputStruct.value()->result ==
                     expectedOutputStructUPtr->result);
        callback_invoked.Notify();
      };

  // Assert that the mocked fetcher will have the method FetchUrl called on it,
  // with the URL being expectedUrl.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      // If and when that happens: DEFINE that the FetchUrl function SHALL do
      // the following:
      //  (This part is NOT an assertion of expected behavior but rather a mock
      //  defining what it shall be)
      .WillOnce([actualResult, &expected_urls](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_TRUE(expected_urls.contains(request.url)) << request.url;
        // Pack said string into a statusOr
        absl::StatusOr<std::string> resp =
            absl::StatusOr<std::string>(actualResult);
        // Now, call the callback (Note: we defined it above!) with the
        // 'response' from the 'server'
        std::move(done_callback)(resp);
      });

  // Finally, actually call the function to perform the test
  CheckGetValuesFromKeysViaHttpClient(std::move(input), {},
                                      std::move(done_callback_to_check_val));
  callback_invoked.WaitForNotification();
}

TEST_F(KeyValueAsyncHttpClientTest, MakesDSPUrlCorrectlyWithDuplicateKey) {
  const GetBuyerValuesInput getValuesClientInput = {
      {"url1", "url1", "url1"}, {}, "www.usatoday.com"};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  const std::string expected_url =
      absl::StrCat(hostname_, "?hostname=www.usatoday.com&keys=url1");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce(
          [expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                  done_callback) { EXPECT_EQ(expected_url, request.url); });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesDSPUrlCorrectlyWithNoKeys) {
  const GetBuyerValuesInput getValuesClientInput = {{}, {}, "www.usatoday.com"};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  const std::string expected_url =
      absl::StrCat(hostname_, "?hostname=www.usatoday.com");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce(
          [expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                  done_callback) { EXPECT_EQ(expected_url, request.url); });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest,
       MakesDSPUrlCorrectlyWithNoHostnameAndClientTypeNone) {
  const GetBuyerValuesInput getValuesClientInput = {
      {"lloyd_george", "clementine", "birkenhead"}, {}, ""};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce([expected_urls_1 = &(expected_urls_1)](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_TRUE(expected_urls_1->contains(request.url));
      });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest,
       MakesDSPUrlCorrectlyWithNoHostnameAndClientTypeBrowser) {
  const GetBuyerValuesInput getValuesClientInput = {
      {"lloyd_george", "clementine", "birkenhead"},
      {},
      "",
      ClientType::CLIENT_TYPE_BROWSER};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  // Note: if the client type is not CLIENT_TYPE_ANDROID, no client_type
  // param is attached to the url. This behavior will change after beta
  // testing to always include a client_type.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce([expected_urls_1 = &(expected_urls_1)](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_TRUE(expected_urls_1->contains(request.url));
      });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesDSPUrlCorrectlyWithClientTypeAndroid) {
  const GetBuyerValuesInput client_input = {
      {"lloyd_george", "clementine", "birkenhead"},
      {},
      "",
      ClientType::CLIENT_TYPE_ANDROID};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(client_input);
  const absl::flat_hash_set<std::string> expected_urls = {
      absl::StrCat(hostname_,
                   "?client_type=1&keys=birkenhead,lloyd_george,clementine"),
      absl::StrCat(hostname_,
                   "?client_type=1&keys=birkenhead,clementine,lloyd_george"),
      absl::StrCat(hostname_,
                   "?client_type=1&keys=lloyd_george,birkenhead,clementine"),
      absl::StrCat(hostname_,
                   "?client_type=1&keys=clementine,birkenhead,lloyd_george"),
      absl::StrCat(hostname_,
                   "?client_type=1&keys=lloyd_george,clementine,birkenhead"),
      absl::StrCat(hostname_,
                   "?client_type=1&keys=clementine,lloyd_george,birkenhead")};
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce([&expected_urls](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_TRUE(expected_urls.contains(request.url));
      });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, AddsMetadataToHeaders) {
  const GetBuyerValuesInput client_input = {
      {"lloyd_george", "clementine", "birkenhead"}, {}, ""};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(client_input);

  // These are Key-Value pairs to be inserted into the headers.
  RequestMetadata expectedMetadata = MakeARandomMap();
  for (const auto& mandatory_header : kMandatoryHeaders) {
    expectedMetadata.insert({mandatory_header.data(), MakeARandomString()});
  }
  // Headers are just single strings
  std::vector<std::string> expectedHeaders;
  for (const auto& expected_metadatum : expectedMetadata) {
    expectedHeaders.emplace_back(
        absl::StrCat(expected_metadatum.first, ":", expected_metadatum.second));
  }
  // Assert that the mocked fetcher will have the method FetchUrl called on it,
  // with the metadata.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce([&expectedHeaders](
                    HTTPRequest request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        std::sort(expectedHeaders.begin(), expectedHeaders.end());
        std::sort(request.headers.begin(), request.headers.end());
        EXPECT_EQ(expectedHeaders, request.headers);
      });

  absl::AnyInvocable<
      void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
      no_check_callback;
  // Finally, actually call the function to perform the test
  CheckGetValuesFromKeysViaHttpClient(std::move(input), expectedMetadata,
                                      std::move(no_check_callback));
}

TEST_F(KeyValueAsyncHttpClientTest, AddsMandatoryHeaders) {
  const GetBuyerValuesInput getValuesClientInput = {
      {"lloyd_george", "clementine", "birkenhead"}, {}, ""};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);

  std::vector<std::string> expected_headers;
  expected_headers.reserve(kMandatoryHeaders.size());
  for (const auto& mandatory_header : kMandatoryHeaders) {
    expected_headers.push_back(absl::StrCat(mandatory_header, ":"));
  }
  // Assert that the mocked fetcher will have the method FetchUrl called on it,
  // with the metadata.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce([&expected_headers](
                    HTTPRequest request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        std::sort(expected_headers.begin(), expected_headers.end());
        std::sort(request.headers.begin(), request.headers.end());
        EXPECT_EQ(expected_headers, request.headers);
      });

  absl::AnyInvocable<
      void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
      no_check_callback;
  // Finally, actually call the function to perform the test.
  CheckGetValuesFromKeysViaHttpClient(std::move(input), {},
                                      std::move(no_check_callback));
}

TEST_F(KeyValueAsyncHttpClientTest, PrewarmsHTTPClient) {
  const std::string expectedUrl = absl::StrCat(hostname_, "?");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  // Create the client.
  BuyerKeyValueAsyncHttpClient kvHttpClient(
      hostname_, std::move(mock_http_fetcher_async_), true);
  absl::SleepFor(absl::Milliseconds(500));
}

TEST_F(KeyValueAsyncHttpClientTest, SpacesInKeysGetEncoded) {
  // Our client will be given this input object.
  const GetBuyerValuesInput getValuesClientInput = {
      {"breaking news"}, {}, "www.usatoday.com"};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  // This is the URL we expect to see built from the input object.
  const std::string expectedUrl =
      absl::StrCat(hostname_,
                   "?hostname=www.usatoday.com&keys="
                   "breaking%20news");
  // Now we define what we expect to get back out of the client, which is a
  // GetBuyerValuesOutput struct.
  // Note that this test is trivial; we use the same string
  const std::string expected_result = R"json({
            "keys": {
              "1j1043317685": {
                "constitution_author": "madison",
                "money_man": "hamilton"
              },
              "1j112014758": {
                "second_president": "adams"
              }
            },
          })json";

  std::unique_ptr<GetBuyerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetBuyerValuesOutput>(
          GetBuyerValuesOutput({expected_result}));

  // Define the lambda function which is the callback.
  // Inside this callback, we will actually check that the client correctly
  // parses what it gets back from the "server" (mocked below).
  absl::Notification callback_invoked;
  absl::AnyInvocable<void(
      absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>)&&>
      done_callback_to_check_val =
          // Capture the expected output struct for comparison
      [&callback_invoked,
       expectedOutputStructUPtr = std::move(expectedOutputStructUPtr)](
          // This is what the client actually passes back
          absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>
              actualOutputStruct) {
        ASSERT_TRUE(actualOutputStruct.ok());
        ASSERT_EQ(actualOutputStruct.value()->result,
                  expectedOutputStructUPtr->result);
        callback_invoked.Notify();
      };

  // Assert that the mocked fetcher will have the method FetchUrl called on it,
  // with the URL being expectedUrl.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      // If and when that happens: DEFINE that the FetchUrl function SHALL do
      // the following:
      //  (This part is NOT an assertion of expected behavior but rather a mock
      //  defining what it shall be)
      .WillOnce([actual_result = expected_result, &expectedUrl](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_EQ(request.url, expectedUrl);
        // Pack said string into a statusOr
        absl::StatusOr<std::string> resp =
            absl::StatusOr<std::string>(actual_result);
        // Now, call the callback (Note: we defined it above!) with the
        // 'response' from the 'server'
        std::move(done_callback)(resp);
      });

  // Finally, actually call the function to perform the test
  CheckGetValuesFromKeysViaHttpClient(std::move(input), {},
                                      std::move(done_callback_to_check_val));
  callback_invoked.WaitForNotification();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
