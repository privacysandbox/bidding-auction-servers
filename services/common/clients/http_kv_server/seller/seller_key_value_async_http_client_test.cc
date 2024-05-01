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

#include "services/common/clients/http_kv_server/seller/seller_key_value_async_http_client.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using test_keys = std::unique_ptr<GetSellerValuesInput>;
using test_on_done = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>;
using test_timeout = absl::Duration;

class KeyValueAsyncHttpClientTest : public testing::Test {
 public:
  static constexpr char hostname_[] = "https://pubads.g.doubleclick.net/td/sts";
  std::unique_ptr<MockHttpFetcherAsync> mock_http_fetcher_async_ =
      std::make_unique<MockHttpFetcherAsync>();
  const absl::flat_hash_set<std::string> expected_urls_ = {
      absl::StrCat(
          hostname_,
          "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
          "3Ffls%3Dtrue%"
          "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
          "3Drtbhfledge,"
          "url2&adComponentRenderUrls=www.foo.com%2Fad%3Fid%3D123%26another_id%"
          "3D456,url4"),
      absl::StrCat(
          hostname_,
          "?renderUrls=url2,https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
          "3Ffls%3Dtrue%"
          "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
          "3Drtbhfledge"
          "&adComponentRenderUrls=www.foo.com%2Fad%3Fid%3D123%26another_id%"
          "3D456,url4"),
      absl::StrCat(hostname_,
                   "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
                   "3Ffls%3Dtrue%"
                   "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
                   "3Drtbhfledge,"
                   "url2&adComponentRenderUrls=url4,www.foo.com%2Fad%3Fid%"
                   "3D123%26another_id%"
                   "3D456"),
      absl::StrCat(
          hostname_,
          "?renderUrls=url2,https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
          "3Ffls%3Dtrue%"
          "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
          "3Drtbhfledge"
          "&adComponentRenderUrls=url4,www.foo.com%2Fad%3Fid%3D123%26another_"
          "id%"
          "3D456")};
  const absl::flat_hash_set<std::string> expected_urls_with_egid_ = {
      absl::StrCat(
          hostname_,
          "?experimentGroupId=1776&renderUrls=https%3A%2F%2Fams.creativecdn."
          "com%2Fcreatives%"
          "3Ffls%3Dtrue%"
          "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
          "3Drtbhfledge,"
          "url2&adComponentRenderUrls=www.foo.com%2Fad%3Fid%3D123%26another_id%"
          "3D456,url4"),
      absl::StrCat(
          hostname_,
          "?experimentGroupId=1776&renderUrls=url2,https%3A%2F%2Fams."
          "creativecdn.com%2Fcreatives%"
          "3Ffls%3Dtrue%"
          "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
          "3Drtbhfledge"
          "&adComponentRenderUrls=www.foo.com%2Fad%3Fid%3D123%26another_id%"
          "3D456,url4"),
      absl::StrCat(hostname_,
                   "?experimentGroupId=1776&renderUrls=https%3A%2F%2Fams."
                   "creativecdn.com%2Fcreatives%"
                   "3Ffls%3Dtrue%"
                   "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
                   "3Drtbhfledge,"
                   "url2&adComponentRenderUrls=url4,www.foo.com%2Fad%3Fid%"
                   "3D123%26another_id%"
                   "3D456"),
      absl::StrCat(
          hostname_,
          "?experimentGroupId=1776&renderUrls=url2,https%3A%2F%2Fams."
          "creativecdn.com%2Fcreatives%"
          "3Ffls%3Dtrue%"
          "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
          "3Drtbhfledge"
          "&adComponentRenderUrls=url4,www.foo.com%2Fad%3Fid%3D123%26another_"
          "id%"
          "3D456")};

 protected:
  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetSellerValuesInput> keys) {
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
        do_nothing_done_callback;
    // Create the client
    SellerKeyValueAsyncHttpClient seller_key_value_async_http_client(
        hostname_, std::move(mock_http_fetcher_async_));
    auto status = seller_key_value_async_http_client.Execute(
        std::move(keys), {}, std::move(do_nothing_done_callback),
        absl::Milliseconds(5000));
    CHECK_OK(status);
  }

  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetSellerValuesInput> keys,
      const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
          value_checking_done_callback) {
    // Create the client
    SellerKeyValueAsyncHttpClient seller_key_value_async_http_client(
        hostname_, std::move(mock_http_fetcher_async_));
    auto status = seller_key_value_async_http_client.Execute(
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
       MakesSSPUrlCorrectlyBasicInputsAndHasCorrectOutput) {
  // Our client will be given this input object.
  const GetSellerValuesInput getValuesClientInput = {
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge",
       "url2"},
      {"www.foo.com/ad?id=123&another_id=456", "url4"},
      ClientType::CLIENT_TYPE_UNKNOWN,
      "1776"};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput);
  // Now we define what we expect to get back out of the client, which is a
  // GetSellerValuesOutput struct.
  // Note that this test is trivial; we use the same string
  const std::string expected_result = R"json({
            "render_urls": {
              "https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge": {
                "constitution_author": "madison",
                "money_man": "hamilton"
              },
              "url2": {
                "second_president": "adams"
              }
            },
            "ad_component_render_urls": {
              "www.foo.com/ad?id=123&another_id=456": {
                "first_president": "washington",
                "chief_of_staff": "marshall"
              },
              "url4": {
                "labour_leader": "attlee",
                "coalition_leader": "david_lloyd_george"
              }
            }
          })json";

  std::unique_ptr<GetSellerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetSellerValuesOutput>(
          GetSellerValuesOutput({expected_result}));

  // Define the lambda function which is the callback.
  // Inside this callback, we will actually check that the client correctly
  // parses what it gets back from the "server" (mocked below).
  absl::Notification callback_invoked;
  absl::AnyInvocable<
      void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
      done_callback_to_check_val =
          // Capture the expected output struct for comparison
      [&callback_invoked,
       expectedOutputStructUPtr = std::move(expectedOutputStructUPtr)](
          // This is what the client actually passes back
          absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>
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
      .WillOnce([actual_result = expected_result,
                 expected_urls = &(expected_urls_with_egid_)](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>)&&>
                        done_callback) {
        EXPECT_TRUE(expected_urls->contains(request.url));
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
       MakesSSPUrlCorrectlyBasicInputsAndFailsForWrongOutput) {
  // Our client will be given this input object.
  const GetSellerValuesInput getValuesClientInput = {
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge",
       "url2"},
      {"www.foo.com/ad?id=123&another_id=456", "url4"}};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput);
  // Now we define what we expect to get back out of the client, which is a
  // GetSellerValuesOutput struct.
  // Note that this test is trivial; we use the same string
  const std::string expectedResult = R"json({
            "render_urls": {
              "https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge": {
                "constitution_author": "madison",
                "money_man": "hamilton"
              },
              "url2": {
                "second_president": "adams"
              }
            },
            "ad_component_render_urls": {
              "www.foo.com/ad?id=123&another_id=456": {
                "first_president": "washington",
                "chief_of_staff": "marshall"
              },
              "url4": {
                "labour_leader": "attlee",
                "coalition_leader": "david_lloyd_george"
              }
            }
          })json";

  const std::string actual_result = R"json({
            "render_urls": {
              "https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge": {
                "constitution_author": "WRONG_NAME",
                "money_man": "hamilton"
              },
              "wrong_render_url": {
                "second_president": "adams"
              }
            },
            "ad_component_render_urls": {
              "wrong_ad_component_render_url1": {
                "first_president": "washington",
                "chief_of_staff": "marshall"
              },
              "wrong_ad_component_render_url2": {
                "labour_leader": "attlee",
                "coalition_leader": "david_lloyd_george"
              }
            }
          })json";
  std::unique_ptr<GetSellerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetSellerValuesOutput>(
          GetSellerValuesOutput({expectedResult}));

  absl::Notification callback_invoked;
  // Define the lambda function which is the callback.
  // Inside this callback, we will actually check that the client correctly
  // parses what it gets back from the "server" (mocked below).
  absl::AnyInvocable<
      void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
      done_callback_to_check_val =
          // Capture the expected output struct for comparison
      [&callback_invoked,
       expectedOutputStructUPtr = std::move(expectedOutputStructUPtr)](
          // This is what the client actually passes back
          absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>
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
      .WillOnce([actual_result, expected_urls = &(expected_urls_)](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>)&&>
                        done_callback) {
        EXPECT_TRUE(expected_urls->contains(request.url));
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
       MakesSSPUrlCorrectlyWithNoAdComponentRenderUrlsAndADuplicateKey) {
  const GetSellerValuesInput getValuesClientInput3 = {
      // NOLINTNEXTLINE
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge",
       "url2", "url3", "url4", "url4"},
      {}};
  std::unique_ptr<GetSellerValuesInput> input3 =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput3);
  const std::string expectedUrl = absl::StrCat(
      hostname_,
      "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
      "3Ffls%3Dtrue%"
      "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge,"
      "url2,url3,url4");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  CheckGetValuesFromKeysViaHttpClient(std::move(input3));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesSSPUrlCorrectlyWithNoClientType) {
  const GetSellerValuesInput client_input = {
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge"},
      {}};
  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(client_input);
  const std::string expected_url =
      absl::StrCat(hostname_,
                   "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
                   "3Ffls%3Dtrue%"
                   "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
                   "3Drtbhfledge");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<std::string>)&&>
                  done_callback) { EXPECT_EQ(request.url, expected_url); });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesSSPUrlCorrectlyWithClientTypeBrowser) {
  const GetSellerValuesInput client_input = {
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge"},
      {},
      ClientType::CLIENT_TYPE_BROWSER};
  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(client_input);

  // Note: if the client type is not CLIENT_TYPE_ANDROID, no client_type
  // param is attached to the url. This behavior will change after beta
  // testing to always include a client_type.
  const std::string expected_url =
      absl::StrCat(hostname_,
                   "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
                   "3Ffls%3Dtrue%"
                   "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
                   "3Drtbhfledge");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<std::string>)&&>
                  done_callback) { EXPECT_EQ(request.url, expected_url); });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesSSPUrlCorrectlyWithClientTypeAndroid) {
  const GetSellerValuesInput client_input = {
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge"},
      {},
      ClientType::CLIENT_TYPE_ANDROID};
  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(client_input);
  const std::string expected_url = absl::StrCat(
      hostname_,
      "?client_type=1&renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
      "3Ffls%3Dtrue%"
      "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
      "3Drtbhfledge");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<std::string>)&&>
                  done_callback) { EXPECT_EQ(request.url, expected_url); });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesSSPUrlCorrectlyWithNoRenderUrls) {
  const GetSellerValuesInput getValuesClientInput3 = {
      {},
      {"https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%"
       "3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge",
       "url2", "url3", "url4"}};
  std::unique_ptr<GetSellerValuesInput> input3 =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput3);
  const std::string expectedUrl = absl::StrCat(
      hostname_,
      "?adComponentRenderUrls=https%3A%2F%2Fams.creativecdn.com%"
      "2Fcreatives%"
      "3Ffls%3Dtrue%26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
      "3Drtbhfledge,url2,url3,url4");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  CheckGetValuesFromKeysViaHttpClient(std::move(input3));
}

TEST_F(KeyValueAsyncHttpClientTest, AddsMetadataToHeaders) {
  RequestMetadata expectedMetadata = MakeARandomMap();
  std::vector<std::string> expectedHeaders;
  for (const auto& it : expectedMetadata) {
    expectedHeaders.emplace_back(absl::StrCat(it.first, ":", it.second));
  }

  const GetSellerValuesInput getValuesClientInput = {
      {},
      {"https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%"
       "3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge",
       "url2", "url3", "url4"}};

  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput);

  // Assert that the mocked fetcher will have the method FetchUrl called on it,
  // with the metadata.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl)
      .WillOnce([&expectedHeaders](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>)&&>
                        done_callback) {
        EXPECT_EQ(expectedHeaders, request.headers);
      });

  absl::AnyInvocable<
      void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
      no_check_callback;
  // Finally, actually call the function to perform the test
  CheckGetValuesFromKeysViaHttpClient(std::move(input), expectedMetadata,
                                      std::move(no_check_callback));
}

TEST_F(KeyValueAsyncHttpClientTest, PrewarmsHTTPClient) {
  const std::string expectedUrl = absl::StrCat(hostname_, "?");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  // Create the client
  SellerKeyValueAsyncHttpClient kvHttpClient(
      hostname_, std::move(mock_http_fetcher_async_), true);
  absl::SleepFor(absl::Milliseconds(500));
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
