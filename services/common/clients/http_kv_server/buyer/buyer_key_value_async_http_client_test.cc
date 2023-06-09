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

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using test_keys = std::unique_ptr<GetBuyerValuesInput>;
using test_on_done = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>;
using test_timeout = absl::Duration;

class KeyValueAsyncHttpClientTest : public testing::Test {
 public:
  const std::string hostname_ = "https://googleads.g.doubleclick.net/td/bts";
  std::unique_ptr<MockHttpFetcherAsync> mock_http_fetcher_async_ =
      std::make_unique<MockHttpFetcherAsync>();

 protected:
  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetBuyerValuesInput> keys) {
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
        do_nothing_done_callback;
    // Create the client
    BuyerKeyValueAsyncHttpClient kvHttpClient(
        hostname_, std::move(mock_http_fetcher_async_));
    kvHttpClient.Execute(std::move(keys), {},
                         std::move(do_nothing_done_callback),
                         absl::Milliseconds(5000));
  }

  void CheckGetValuesFromKeysViaHttpClient(
      std::unique_ptr<GetBuyerValuesInput> keys,
      const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
          value_checking_done_callback) {
    // Create the client
    BuyerKeyValueAsyncHttpClient kvHttpClient(
        hostname_, std::move(mock_http_fetcher_async_));
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
       MakesDSPUrlCorrectlyBasicInputsAndHasCorrectOutput) {
  // Our client will be given this input object.
  const GetBuyerValuesInput getValuesClientInput = {
      {"1j1043317685", "1j112014758"}, "www.usatoday.com"};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  // This is the URL we expect to see built from the input object.
  const std::string expectedUrl =
      hostname_ + "?hostname=www.usatoday.com&keys=1j1043317685,1j112014758";
  // Now we define what we expect to get back out of the client, which is a
  // GetBuyerValuesOutput struct.
  // Note that this test is trivial; we use the same string
  const std::string expectedResult = R"json({
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

  const std::string actualResult = expectedResult;
  std::unique_ptr<GetBuyerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetBuyerValuesOutput>(
          GetBuyerValuesOutput({expectedResult}));

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
       MakesDSPUrlCorrectlyBasicInputsAndFailsForWrongOutput) {
  // Our client will be given this input object.
  const GetBuyerValuesInput getValuesClientInput = {
      {"1j1043317685", "1j112014758"}, "www.usatoday.com"};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  // This is the URL we expect to see built from the input object.
  const std::string expectedUrl =
      hostname_ + "?hostname=www.usatoday.com&keys=1j1043317685,1j112014758";
  // Now we define what we expect to get back out of the client, which is a
  // GetBuyerValuesOutput struct.
  // Note that this test is trivial; we use the same string
  const std::string expectedResult = R"json({
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

  const std::string actualResult = R"json({
            "keys": {
              "1j1043317685": {
                "constitution_author": "Edmund Burke",
                "money_man": "Adam Smith"
              },
              "1j112014758": {
                "second_president": "Sir Henry Pelham"
              }
            },
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

TEST_F(KeyValueAsyncHttpClientTest, MakesDSPUrlCorrectlyWithManyLongKeys) {
  const GetBuyerValuesInput getValuesClientInput = {
      {"a", "banana", "winston_churchill", "flibbertigibbet", "balfour", "eden",
       "attlee", "chaimberlain", "halifax", "dunkirk", "dowding", "roosevelt",
       "curzon"},
      "www.usatoday.com"};
  std::unique_ptr<GetBuyerValuesInput> input3 =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  const std::string expectedUrl =
      hostname_ +
      "?hostname=www.usatoday.com&keys=a,"
      "banana,"
      "winston_churchill,flibbertigibbet,balfour,eden,attlee,chaimberlain,"
      "halifax,dunkirk,dowding,roosevelt,curzon";
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  CheckGetValuesFromKeysViaHttpClient(std::move(input3));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesDSPUrlCorrectlyWithNoKeys) {
  const GetBuyerValuesInput getValuesClientInput = {{}, "www.usatoday.com"};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  const std::string expectedUrl = hostname_ + "?hostname=www.usatoday.com";
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, MakesDSPUrlCorrectlyWithNoHostname) {
  const GetBuyerValuesInput getValuesClientInput = {
      {"lloyd_george", "clementine", "birkenhead"}, ""};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  const std::string expectedUrl =
      hostname_ + "?keys=lloyd_george,clementine,birkenhead";
  EXPECT_CALL(*mock_http_fetcher_async_, FetchUrl).Times(1);
  CheckGetValuesFromKeysViaHttpClient(std::move(input));
}

TEST_F(KeyValueAsyncHttpClientTest, AddsMetadataToHeaders) {
  const GetBuyerValuesInput getValuesClientInput = {
      {"lloyd_george", "clementine", "birkenhead"}, ""};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);

  RequestMetadata expectedMetadata = MakeARandomMap();
  for (const auto& it : kMandatoryHeaders) {
    expectedMetadata.insert({it.data(), MakeARandomString()});
  }
  std::vector<std::string> expectedHeaders;
  for (const auto& it : expectedMetadata) {
    expectedHeaders.emplace_back(absl::StrCat(it.first, ":", it.second));
  }
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
      void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
      no_check_callback;
  // Finally, actually call the function to perform the test
  CheckGetValuesFromKeysViaHttpClient(std::move(input), expectedMetadata,
                                      std::move(no_check_callback));
}

TEST_F(KeyValueAsyncHttpClientTest, AddsMandatoryHeaders) {
  const GetBuyerValuesInput getValuesClientInput = {
      {"lloyd_george", "clementine", "birkenhead"}, ""};
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);

  std::vector<std::string> expectedHeaders;
  for (const auto& it : kMandatoryHeaders) {
    expectedHeaders.push_back(absl::StrCat(it, ":"));
  }
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
      {"1j1043317685", "1j112014758", "breaking news"}, "www.usatoday.com"};
  // We must transform it to a unique ptr to match the function signature.
  std::unique_ptr<GetBuyerValuesInput> input =
      std::make_unique<GetBuyerValuesInput>(getValuesClientInput);
  // This is the URL we expect to see built from the input object.
  const std::string expectedUrl = hostname_ +
                                  "?hostname=www.usatoday.com&keys="
                                  "1j1043317685,1j112014758,breaking%20news";
  // Now we define what we expect to get back out of the client, which is a
  // GetBuyerValuesOutput struct.
  // Note that this test is trivial; we use the same string
  const std::string expectedResult = R"json({
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

  const std::string actualResult = expectedResult;
  std::unique_ptr<GetBuyerValuesOutput> expectedOutputStructUPtr =
      std::make_unique<GetBuyerValuesOutput>(
          GetBuyerValuesOutput({expectedResult}));

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

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
