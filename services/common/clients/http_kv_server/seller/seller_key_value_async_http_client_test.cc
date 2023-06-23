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
  std::string hostname_ = "https://pubads.g.doubleclick.net/td/sts";
  std::unique_ptr<MockHttpFetcherAsync> mock_http_fetcher_async_ =
      std::make_unique<MockHttpFetcherAsync>();

 protected:
  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetSellerValuesInput> keys) {
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
        do_nothing_done_callback;
    // Create the client
    SellerKeyValueAsyncHttpClient kvHttpClient(
        this->hostname_, std::move(this->mock_http_fetcher_async_));
    kvHttpClient.Execute(std::move(keys), {},
                         std::move(do_nothing_done_callback),
                         absl::Milliseconds(5000));
  }

  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetSellerValuesInput> keys,
      const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
          value_checking_done_callback) {
    // Create the client
    SellerKeyValueAsyncHttpClient kvHttpClient(
        this->hostname_, std::move(this->mock_http_fetcher_async_));
    kvHttpClient.Execute(std::move(keys), metadata,
                         std::move(value_checking_done_callback),
                         absl::Milliseconds(5000));
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
      {"url3", "url4"}};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput);
  // This is the URL we expect to see built from the input object.
  const std::string expectedUrl =
      this->hostname_ +
      "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%"
      "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge,"
      "url2&adComponentRenderUrls=url3,url4";

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
              "url3": {
                "first_president": "washington",
                "chief_of_staff": "marshall"
              },
              "url4": {
                "labour_leader": "attlee",
                "coalition_leader": "david_lloyd_george"
              }
            }
          })json";

  const std::string actualResult = expectedResult;
  std::unique_ptr<GetSellerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetSellerValuesOutput>(
          GetSellerValuesOutput({expectedResult}));

  // Define the lambda function which is the callback.
  // Inside this callback, we will actually check that the client correctly
  // parses what it gets back from the "server" (mocked below).
  absl::Notification callback_invoked;
  absl::AnyInvocable<void(
      absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>)&&>
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
      .WillOnce([actualResult, &expectedUrl](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_STREQ(request.url.c_str(), expectedUrl.c_str());
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

TEST_F(KeyValueAsyncHttpClientTest,
       MakesSSPUrlCorrectlyBasicInputsAndFailsForWrongOutput) {
  // Our client will be given this input object.
  const GetSellerValuesInput getValuesClientInput = {
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge",
       "url2"},
      {"url3", "url4"}};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput);
  // This is the URL we expect to see built from the input object.
  const std::string expectedUrl =
      this->hostname_ +
      "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%"
      "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge,"
      "url2&adComponentRenderUrls=url3,url4";

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
              "url3": {
                "first_president": "washington",
                "chief_of_staff": "marshall"
              },
              "url4": {
                "labour_leader": "attlee",
                "coalition_leader": "david_lloyd_george"
              }
            }
          })json";

  const std::string actualResult = R"json({
            "render_urls": {
              "https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge_WRONG_ENDING": {
                "constitution_author": "madison",
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
  absl::AnyInvocable<void(
      absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>)&&>
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
      .WillOnce([actualResult, &expectedUrl](
                    HTTPRequest request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_STREQ(request.url.c_str(), expectedUrl.c_str());
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

TEST_F(KeyValueAsyncHttpClientTest,
       MakesSSPUrlCorrectlyWithNoAdComponentRenderUrls) {
  const GetSellerValuesInput getValuesClientInput3 = {
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge",
       "url2", "url3", "url4"},
      {}};
  std::unique_ptr<GetSellerValuesInput> input3 =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput3);
  const std::string expectedUrl =
      this->hostname_ +
      "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%"
      "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge,"
      "url2,url3,url4";
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  CheckGetValuesFromKeysViaHttpClient(std::move(input3));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesSSPUrlCorrectlyWithNoRenderUrls) {
  const GetSellerValuesInput getValuesClientInput3 = {
      {},
      {"https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%"
       "3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge",
       "url2", "url3", "url4"}};
  std::unique_ptr<GetSellerValuesInput> input3 =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput3);
  const std::string expectedUrl =
      this->hostname_ +
      "?adComponentRenderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
      "3Ffls%3Dtrue%26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
      "3Drtbhfledge,url2,url3,url4";
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
                    HTTPRequest request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
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
  const std::string expectedUrl = absl::StrCat(this->hostname_, "?");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  // Create the client
  SellerKeyValueAsyncHttpClient kvHttpClient(
      this->hostname_, std::move(this->mock_http_fetcher_async_), true);
  absl::SleepFor(absl::Milliseconds(500));
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
