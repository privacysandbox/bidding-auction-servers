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

#include "services/seller_frontend_service/providers/http_scoring_signals_async_provider.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::An;

TEST(HttpScoringSignalsAsyncProviderTest, MapsAdKeysToSellerValuesInput) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetSellerValuesInput, GetSellerValuesOutput,
                      GetSellerValuesRawInput, GetSellerValuesRawOutput>>();
  BuyerBidsResponseMap buyer_bids_map;
  std::vector<std::string> ad_render_urls;
  std::vector<std::string> ad_component_render_urls;
  // Construct input for provider and seller key value.
  int num_buyers = 2;
  int num_ad_with_bids_per_buyer = 2;
  int num_ad_components_per_ad = 2;
  for (int buyer_index = 0; buyer_index < num_buyers; buyer_index++) {
    std::string buyer_ig_owner = MakeARandomString();
    auto get_bid_res = std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    for (int ad_with_bid_index = 0;
         ad_with_bid_index < num_ad_with_bids_per_buyer; ad_with_bid_index++) {
      std::string url = MakeARandomString();
      AdWithBid* ad_with_bid = get_bid_res->mutable_bids()->Add();
      ad_with_bid->set_render(url);
      ad_render_urls.emplace_back(url);
      for (int ad_component_index = 0;
           ad_component_index < num_ad_components_per_ad;
           ad_component_index++) {
        std::string new_ad_comp_url = MakeARandomString();
        std::string* ad_component_render_url_to_add =
            ad_with_bid->mutable_ad_component_render()->Add();
        *ad_component_render_url_to_add = new_ad_comp_url;
        ad_component_render_urls.emplace_back(new_ad_comp_url);
      }
    }
    buyer_bids_map.try_emplace(buyer_ig_owner, std::move(get_bid_res));
  }

  absl::Notification notification;
  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetSellerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetSellerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce([&ad_render_urls, &ad_component_render_urls, &notification](
                    std::unique_ptr<GetSellerValuesInput> input,
                    const RequestMetadata& metadata,
                    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                                GetSellerValuesOutput>>) &&>
                        callback,
                    absl::Duration timeout) {
        // All ads from all responses were sent to KV client.
        // Order is irrelevant but may have been changed, so sort.
        std::sort(input->render_urls.begin(), input->render_urls.end());
        std::sort(ad_render_urls.begin(), ad_render_urls.end());
        EXPECT_EQ(input->render_urls, ad_render_urls);
        // And again for ad component render urls:
        std::sort(input->ad_component_render_urls.begin(),
                  input->ad_component_render_urls.end());
        std::sort(ad_component_render_urls.begin(),
                  ad_component_render_urls.end());
        EXPECT_EQ(input->ad_component_render_urls, ad_component_render_urls);
        notification.Notify();
        return absl::OkStatus();
      });

  HttpScoringSignalsAsyncProvider class_under_test(std::move(mock_client));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, {});
  class_under_test.Get(
      scoring_signals_request,
      [](absl::StatusOr<std::unique_ptr<ScoringSignals>> signals) {},
      absl::Milliseconds(100));
  notification.WaitForNotification();
}

TEST(HttpScoringSignalsAsyncProviderTest, MapsAsyncClientError) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetSellerValuesInput, GetSellerValuesOutput,
                      GetSellerValuesRawInput, GetSellerValuesRawOutput>>();
  BuyerBidsResponseMap buyer_bids_map;
  absl::Notification notification;

  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetSellerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetSellerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce(
          [](std::unique_ptr<GetSellerValuesInput> input,
             const RequestMetadata& metadata,
             absl::AnyInvocable<
                 void(
                     absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>) &&>
                 callback,
             absl::Duration timeout) {
            (std::move(callback))(absl::InternalError(""));
            return absl::OkStatus();
          });

  HttpScoringSignalsAsyncProvider class_under_test(std::move(mock_client));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, {});

  class_under_test.Get(
      scoring_signals_request,
      [&notification](absl::StatusOr<std::unique_ptr<ScoringSignals>> signals) {
        EXPECT_EQ(signals.status().code(), absl::StatusCode::kInternal);
        notification.Notify();
      },
      absl::Milliseconds(100));
  notification.WaitForNotification();
}

TEST(HttpScoringSignalsAsyncProviderTest, MapsResponseToScoringSignals) {
  auto mock_client = std::make_unique<
      AsyncClientMock<GetSellerValuesInput, GetSellerValuesOutput,
                      GetSellerValuesRawInput, GetSellerValuesRawOutput>>();
  BuyerBidsResponseMap buyer_bids_map;
  absl::Notification notification;
  std::string expected_output = MakeARandomString();

  EXPECT_CALL(
      *mock_client,
      Execute(An<std::unique_ptr<GetSellerValuesInput>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GetSellerValuesOutput>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce([&expected_output](
                    std::unique_ptr<GetSellerValuesInput> input,
                    const RequestMetadata& metadata,
                    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                                GetSellerValuesOutput>>) &&>
                        callback,
                    absl::Duration timeout) {
        auto output = std::make_unique<GetSellerValuesOutput>();
        output->result = expected_output;
        (std::move(callback))(std::move(output));
        return absl::OkStatus();
      });

  HttpScoringSignalsAsyncProvider class_under_test(std::move(mock_client));

  ScoringSignalsRequest scoring_signals_request =
      ScoringSignalsRequest(buyer_bids_map, {});

  class_under_test.Get(
      scoring_signals_request,
      [&expected_output,
       &notification](absl::StatusOr<std::unique_ptr<ScoringSignals>> signals) {
        EXPECT_STREQ(signals.value()->scoring_signals->c_str(),
                     expected_output.c_str());
        notification.Notify();
      },
      absl::Milliseconds(100));
  notification.WaitForNotification();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
