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

#include "services/buyer_frontend_service/providers/http_bidding_signals_async_provider.h"

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/common/clients/http_kv_server/buyer/buyer_key_value_async_http_client.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::An;

GetBidsRequest::GetBidsRawRequest GetRequest() {
  GetBidsRequest::GetBidsRawRequest request;
  for (int i = 0; i < MakeARandomInt(1, 10); i++) {
    *request.mutable_buyer_input()->add_interest_groups() =
        MakeARandomInterestGroupFromBrowser();
  }
  return request;
}

TEST(HttpBiddingSignalsAsyncProviderTest, MapsGetBidKeysToBuyerValuesInput) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetBuyerValuesInput, GetBuyerValuesOutput,
                      GetBuyerValuesRawInput, GetBuyerValuesRawOutput>>();
  auto request = GetRequest();
  absl::Notification notification;

  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetBuyerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetBuyerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce(
          [&request, &notification](
              std::unique_ptr<GetBuyerValuesInput> input,
              const RequestMetadata& metadata,
              absl::AnyInvocable<
                  void(
                      absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
                  callback,
              absl::Duration timeout) {
            // Check that keys received in input are from buyer_input
            EXPECT_STREQ(input->hostname.data(),
                         request.publisher_name().data());
            absl::flat_hash_set<std::string> received_keys(input->keys.begin(),
                                                           input->keys.end());

            // Collect all expected keys
            absl::flat_hash_set<std::string> expected_keys;
            for (const auto& ca : request.buyer_input().interest_groups()) {
              expected_keys.emplace(ca.name());
              expected_keys.insert(ca.bidding_signals_keys().begin(),
                                   ca.bidding_signals_keys().end());
            }

            // All expected keys in received keys and vice versa
            for (auto& key : expected_keys) {
              EXPECT_TRUE(received_keys.contains(key));
            }
            for (auto& key : received_keys) {
              EXPECT_TRUE(expected_keys.contains(key));
            }
            notification.Notify();
            return absl::OkStatus();
          });

  HttpBiddingSignalsAsyncProvider class_under_test(std::move(mock_client));

  BiddingSignalsRequest bidding_signals_request(request, {});
  class_under_test.Get(
      bidding_signals_request,
      [](absl::StatusOr<std::unique_ptr<BiddingSignals>> signals) {},
      absl::Milliseconds(100));
  notification.WaitForNotification();
}

TEST(HttpBiddingSignalsAsyncProviderTest, MapsBuyerValuesAsyncClientError) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetBuyerValuesInput, GetBuyerValuesOutput,
                      GetBuyerValuesRawInput, GetBuyerValuesRawOutput>>();
  auto request = GetRequest();
  absl::Notification notification;

  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetBuyerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetBuyerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce(
          [](std::unique_ptr<GetBuyerValuesInput> input,
             const RequestMetadata& metadata,
             absl::AnyInvocable<
                 void(absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
                 callback,
             absl::Duration timeout) {
            (std::move(callback))(absl::InternalError(""));
            return absl::OkStatus();
          });

  HttpBiddingSignalsAsyncProvider class_under_test(std::move(mock_client));

  BiddingSignalsRequest bidding_signals_request(request, {});
  class_under_test.Get(
      bidding_signals_request,
      [&notification](absl::StatusOr<std::unique_ptr<BiddingSignals>> signals) {
        EXPECT_EQ(signals.status().code(), absl::StatusCode::kInternal);
        notification.Notify();
      },
      absl::Milliseconds(100));
  notification.WaitForNotification();
}

TEST(HttpBiddingSignalsAsyncProviderTest,
     MapsBuyerValuesAsyncClientResponseToBiddingSignals) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetBuyerValuesInput, GetBuyerValuesOutput,
                      GetBuyerValuesRawInput, GetBuyerValuesRawOutput>>();
  auto request = GetRequest();
  *request.mutable_buyer_input()->add_interest_groups() =
      MakeARandomInterestGroupFromBrowser();
  absl::Notification notification;
  std::string expected_output = MakeARandomString();

  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetBuyerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetBuyerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce(
          [&expected_output](
              std::unique_ptr<GetBuyerValuesInput> input,
              const RequestMetadata& metadata,
              absl::AnyInvocable<
                  void(
                      absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>) &&>
                  callback,
              absl::Duration timeout) {
            auto output = std::make_unique<GetBuyerValuesOutput>();
            output->result = expected_output;
            (std::move(callback))(std::move(output));
            return absl::OkStatus();
          });

  HttpBiddingSignalsAsyncProvider class_under_test(std::move(mock_client));

  BiddingSignalsRequest bidding_signals_request(request, {});
  class_under_test.Get(
      bidding_signals_request,
      [&expected_output,
       &notification](absl::StatusOr<std::unique_ptr<BiddingSignals>> signals) {
        EXPECT_STREQ(signals.value()->trusted_signals->c_str(),
                     expected_output.c_str());
        notification.Notify();
      },
      absl::Milliseconds(100));
  notification.WaitForNotification();
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
