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

#include <algorithm>

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

inline constexpr uint32_t kDataVersionHeaderValue = 1689;
constexpr uint64_t kTooBigDataVersionHeaderValue =
    uint64_t(UINT32_MAX) + uint64_t(UINT16_MAX);
inline constexpr absl::string_view kIvalidDataVersionHeaderStringValue =
    "abcxyz";
// Don't check these. They are not calculated from the request or response.
inline constexpr size_t kRequestSizeDontCheck = 0;
inline constexpr size_t kResponseSizeDontCheck = 0;
inline constexpr char kHostname[] = "https://trustedhost.com";
inline constexpr char kExpectedResponseBodyJsonString[] = R"json({
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

const absl::flat_hash_set<std::string> kExpectedUrls = {
    absl::StrCat(
        kHostname,
        "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
        "3Ffls%3Dtrue%"
        "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
        "3Drtbhfledge,"
        "url2&adComponentRenderUrls=www.foo.com%2Fad%3Fid%3D123%26another_id%"
        "3D456,url4"),
    absl::StrCat(
        kHostname,
        "?renderUrls=url2,https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
        "3Ffls%3Dtrue%"
        "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
        "3Drtbhfledge"
        "&adComponentRenderUrls=www.foo.com%2Fad%3Fid%3D123%26another_id%"
        "3D456,url4"),
    absl::StrCat(kHostname,
                 "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
                 "3Ffls%3Dtrue%"
                 "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
                 "3Drtbhfledge,"
                 "url2&adComponentRenderUrls=url4,www.foo.com%2Fad%3Fid%"
                 "3D123%26another_id%"
                 "3D456"),
    absl::StrCat(
        kHostname,
        "?renderUrls=url2,https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
        "3Ffls%3Dtrue%"
        "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
        "3Drtbhfledge"
        "&adComponentRenderUrls=url4,www.foo.com%2Fad%3Fid%3D123%26another_"
        "id%"
        "3D456")};
const absl::flat_hash_set<std::string> expected_urls_with_egid_ = {
    absl::StrCat(
        kHostname,
        "?experimentGroupId=1776&renderUrls=https%3A%2F%2Fams.creativecdn."
        "com%2Fcreatives%"
        "3Ffls%3Dtrue%"
        "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
        "3Drtbhfledge,"
        "url2&adComponentRenderUrls=www.foo.com%2Fad%3Fid%3D123%26another_id%"
        "3D456,url4"),
    absl::StrCat(
        kHostname,
        "?experimentGroupId=1776&renderUrls=url2,https%3A%2F%2Fams."
        "creativecdn.com%2Fcreatives%"
        "3Ffls%3Dtrue%"
        "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
        "3Drtbhfledge"
        "&adComponentRenderUrls=www.foo.com%2Fad%3Fid%3D123%26another_id%"
        "3D456,url4"),
    absl::StrCat(kHostname,
                 "?experimentGroupId=1776&renderUrls=https%3A%2F%2Fams."
                 "creativecdn.com%2Fcreatives%"
                 "3Ffls%3Dtrue%"
                 "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
                 "3Drtbhfledge,"
                 "url2&adComponentRenderUrls=url4,www.foo.com%2Fad%3Fid%"
                 "3D123%26another_id%"
                 "3D456"),
    absl::StrCat(
        kHostname,
        "?experimentGroupId=1776&renderUrls=url2,https%3A%2F%2Fams."
        "creativecdn.com%2Fcreatives%"
        "3Ffls%3Dtrue%"
        "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
        "3Drtbhfledge"
        "&adComponentRenderUrls=url4,www.foo.com%2Fad%3Fid%3D123%26another_"
        "id%"
        "3D456")};

class KeyValueAsyncHttpClientTest : public testing::Test {
 public:
  std::unique_ptr<MockHttpFetcherAsync> mock_http_fetcher_async_ =
      std::make_unique<MockHttpFetcherAsync>();

 protected:
  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetSellerValuesInput> keys) {
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
        do_nothing_done_callback;
    // Create the client
    SellerKeyValueAsyncHttpClient seller_key_value_async_http_client(
        kHostname, std::move(mock_http_fetcher_async_));
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
        kHostname, std::move(mock_http_fetcher_async_));
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
 * 4. Actual client calls FetchUrlWithMetadata with that url it made
 * 5. Since FetchUrlWithMetadata is a mock, we get to assert that it is called
 * with the url we expect (or in other words: assert that the actual URL the
 * client created matches the expected URL we wrote)
 * 6. If and when all of that goes well: Since the HttpAsyncFetcher is a mock,
 * we define its behavior
 * 7. We specify that it returns a string like a KV server would
 * 8. Then we pass that string into a callback exactly as the real
 * FetchUrlWithMetadata would have done
 * 9. THAT callback, into which the output of our mock FetchUrlWithMetadata is
 * passed, is the one actually defined to be the actual callback made in the
 * actual client.
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

  std::unique_ptr<GetSellerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetSellerValuesOutput>(GetSellerValuesOutput(
          {kExpectedResponseBodyJsonString, kRequestSizeDontCheck,
           kResponseSizeDontCheck, kDataVersionHeaderValue}));

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
        // We can't make any assertions here because if we do and they fail the
        // callback will never be notified.
        EXPECT_TRUE(actualOutputStruct.ok());
        if (actualOutputStruct.ok()) {
          EXPECT_EQ((*actualOutputStruct)->result,
                    expectedOutputStructUPtr->result);
          EXPECT_EQ((*actualOutputStruct)->data_version,
                    expectedOutputStructUPtr->data_version);
        }
        callback_invoked.Notify();
      };

  // Assert that the mocked fetcher will have the method FetchUrlWithMetadata
  // called on it, with the URL being expectedUrl.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      // If and when that happens: DEFINE that the FetchUrlWithMetadata function
      // SHALL do the following:
      //  (This part is NOT an assertion of expected behavior but rather a mock
      //  defining what it shall be)
      .WillOnce([expected_urls = &expected_urls_with_egid_](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
                        done_callback) {
        EXPECT_TRUE(expected_urls->contains(request.url));

        HTTPResponse actual_http_response;
        actual_http_response.body = kExpectedResponseBodyJsonString;
        // Add a valid header.
        absl::flat_hash_map<std::string, absl::StatusOr<std::string>> headers;
        headers[kDataVersionResponseHeaderName] = absl::StatusOr<std::string>(
            absl::StrFormat("%d", kDataVersionHeaderValue));
        actual_http_response.headers = headers;
        // Now, call the callback (Note: we defined it above!) with the
        // 'response' from the 'server'
        std::move(done_callback)(actual_http_response);
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

  const std::string actual_response_body_json_string = R"json({
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
      std::make_unique<GetSellerValuesOutput>(GetSellerValuesOutput(
          {kExpectedResponseBodyJsonString, kRequestSizeDontCheck,
           kResponseSizeDontCheck, /*data_version=*/0}));

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
        // We can't make any assertions here because if we do and they fail the
        // callback will never be notified.
        EXPECT_TRUE(actualOutputStruct.ok());
        if (actualOutputStruct.ok()) {
          EXPECT_NE((*actualOutputStruct)->result,
                    expectedOutputStructUPtr->result);
          EXPECT_EQ((*actualOutputStruct)->data_version,
                    expectedOutputStructUPtr->data_version);
        }
        callback_invoked.Notify();
      };

  // Assert that the mocked fetcher will have the method FetchUrlWithMetadata
  // called on it, with the URL being expectedUrl.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      // If and when that happens: DEFINE that the FetchUrlWithMetadata function
      // SHALL do the following:
      //  (This part is NOT an assertion of expected behavior but rather a mock
      //  defining what it shall be)
      .WillOnce([actual_response_body_json_string](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
                        done_callback) {
        EXPECT_TRUE(kExpectedUrls.contains(request.url));
        HTTPResponse actual_http_response;
        actual_http_response.body = actual_response_body_json_string;
        // Add a header value that is INVALID.
        // The idea is that it will NOT parse but will also NOT CRASH the
        // client.
        absl::flat_hash_map<std::string, absl::StatusOr<std::string>> headers;
        headers[kDataVersionResponseHeaderName] =
            absl::StatusOr<std::string>(kIvalidDataVersionHeaderStringValue);
        actual_http_response.headers = headers;
        // Now, call the callback (Note: we defined it above!) with the
        // 'response' from the 'server'
        std::move(done_callback)(actual_http_response);
      });

  // Finally, actually call the function to perform the test
  CheckGetValuesFromKeysViaHttpClient(std::move(input), {},
                                      std::move(done_callback_to_check_val));
  callback_invoked.WaitForNotification();
}

TEST_F(KeyValueAsyncHttpClientTest,
       MakesSSPUrlCorrectlyWithNoAdComponentRenderUrlsAndADuplicateKey) {
  const GetSellerValuesInput get_values_client_input = {
      // NOLINTNEXTLINE
      {"https://ams.creativecdn.com/"
       "creatives?fls=true&id=000rHxs8auvkixVNAyYw&c=VMR8favvTg6zsLGCra37&s="
       "rtbhfledge",
       "url2", "url3", "url4", "url4"},
      {}};
  std::unique_ptr<GetSellerValuesInput> input3 =
      std::make_unique<GetSellerValuesInput>(get_values_client_input);
  const std::string expected_url = absl::StrCat(
      kHostname,
      "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
      "3Ffls%3Dtrue%"
      "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge,"
      "url2,url3,url4");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
                  done_callback) { EXPECT_EQ(request.url, expected_url); });
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
      absl::StrCat(kHostname,
                   "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
                   "3Ffls%3Dtrue%"
                   "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
                   "3Drtbhfledge");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
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
      absl::StrCat(kHostname,
                   "?renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
                   "3Ffls%3Dtrue%"
                   "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
                   "3Drtbhfledge");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
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
      kHostname,
      "?client_type=1&renderUrls=https%3A%2F%2Fams.creativecdn.com%2Fcreatives%"
      "3Ffls%3Dtrue%"
      "26id%3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%"
      "3Drtbhfledge");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
                  done_callback) { EXPECT_EQ(request.url, expected_url); });
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesSSPUrlCorrectlyWithNoRenderUrls) {
  const GetSellerValuesInput get_values_client_input = {
      {},
      {"https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%"
       "3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge",
       "url2", "url3", "url4"}};
  std::unique_ptr<GetSellerValuesInput> input3 =
      std::make_unique<GetSellerValuesInput>(get_values_client_input);
  const std::string expected_url = absl::StrCat(
      kHostname,
      "?adComponentRenderUrls=https%253A%252F%252Fams.creativecdn.com%252F"
      "creatives%253Ffls%253Dtrue"
      "%2526id%253D000rHxs8auvkixVNAyYw%2526c%253DVMR8favvTg6zsLGCra37%2526s"
      "%253Drtbhfledge,url2,url3,url4");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
                  done_callback) { EXPECT_EQ(request.url, expected_url); });
  CheckGetValuesFromKeysViaHttpClient(std::move(input3));
}

TEST_F(KeyValueAsyncHttpClientTest,
       AddsMetadataToHeadersAndContainsDVInIncludeHeaders) {
  RequestMetadata expected_metadata = MakeARandomMap();
  std::vector<std::string> expected_headers;
  for (const auto& it : expected_metadata) {
    expected_headers.emplace_back(absl::StrCat(it.first, ":", it.second));
  }

  const GetSellerValuesInput getValuesClientInput = {
      {},
      {"https%3A%2F%2Fams.creativecdn.com%2Fcreatives%3Ffls%3Dtrue%26id%"
       "3D000rHxs8auvkixVNAyYw%26c%3DVMR8favvTg6zsLGCra37%26s%3Drtbhfledge",
       "url2", "url3", "url4"}};

  std::unique_ptr<GetSellerValuesInput> input =
      std::make_unique<GetSellerValuesInput>(getValuesClientInput);

  // Assert that the mocked fetcher will have the method FetchUrlWithMetadata
  // called on it, with the metadata.
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      .WillOnce([&expected_headers](
                    const HTTPRequest& request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
                        done_callback) {
        EXPECT_EQ(expected_headers, request.headers);
        EXPECT_TRUE(std::find(request.include_headers.begin(),
                              request.include_headers.end(),
                              kDataVersionResponseHeaderName) !=
                    request.include_headers.end());
        HTTPResponse actual_http_response;
        actual_http_response.body = kExpectedResponseBodyJsonString;
        // Add an INVALID header, one which EXCEEDS even the 32-bit limit!
        absl::flat_hash_map<std::string, absl::StatusOr<std::string>> headers;
        headers[kDataVersionResponseHeaderName] = absl::StatusOr<std::string>(
            absl::StrFormat("%d", kTooBigDataVersionHeaderValue));
        actual_http_response.headers = headers;
        // Now, call the callback (Note: we defined it above!) with the
        // 'response' from the 'server'
        std::move(done_callback)(actual_http_response);
      });

  // Expect the data version to be 0 as the value above will not even fit in 32
  // bits.
  std::unique_ptr<GetSellerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetSellerValuesOutput>(GetSellerValuesOutput(
          {kExpectedResponseBodyJsonString, kRequestSizeDontCheck,
           kResponseSizeDontCheck, /*data_version=*/0}));

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
        // We can't make any assertions here because if we do and they fail the
        // callback will never be notified.
        EXPECT_TRUE(actualOutputStruct.ok());
        if (actualOutputStruct.ok()) {
          EXPECT_EQ((*actualOutputStruct)->result,
                    expectedOutputStructUPtr->result);
          EXPECT_EQ((*actualOutputStruct)->data_version,
                    expectedOutputStructUPtr->data_version);
        }
        callback_invoked.Notify();
      };

  // Finally, actually call the function to perform the test
  CheckGetValuesFromKeysViaHttpClient(std::move(input), expected_metadata,
                                      std::move(done_callback_to_check_val));
  callback_invoked.WaitForNotification();
}

TEST_F(KeyValueAsyncHttpClientTest, PrewarmsHTTPClient) {
  const std::string expected_url = absl::StrCat(kHostname, "?");
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrlWithMetadata)
      .WillOnce(
          [&expected_url](
              const HTTPRequest& request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)&&>
                  done_callback) { EXPECT_EQ(request.url, expected_url); });
  // Create the client
  SellerKeyValueAsyncHttpClient kv_http_client(
      kHostname, std::move(mock_http_fetcher_async_), true);
  absl::SleepFor(absl::Milliseconds(500));
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
