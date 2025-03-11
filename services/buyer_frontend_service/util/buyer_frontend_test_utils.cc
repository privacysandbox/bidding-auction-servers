//   Copyright 2022 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include "services/buyer_frontend_service/util/buyer_frontend_test_utils.h"

#include <gmock/gmock-matchers.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <include/gmock/gmock-actions.h>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/buyer_frontend_service/providers/bidding_signals_async_provider.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::testing::_;
using ::testing::An;
using ::testing::AnyNumber;

inline constexpr char kByosOutput[] = "byos_output";

void SetupBiddingProviderMock(
    const MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>& provider,
    const BiddingProviderMockOptions& options) {
  // Note: options has to be passed by value, performing a copy, since the
  // lambda will run after the factory has gone out of scope. We can afford the
  // performance hit since this is for testing. Changing this to a pass by
  // reference is unsafe.
  auto MockBiddingSignalsProvider =
      [options](
          const BiddingSignalsRequest& bidding_signals_request,
          absl::AnyInvocable<void(
              absl::StatusOr<std::unique_ptr<BiddingSignals>>, GetByteSize)&&>
              on_done,
          absl::Duration timeout, RequestContext context) {
        GetByteSize get_byte_size;
        if (options.server_error_to_return) {
          std::move(on_done)(*(options.server_error_to_return), get_byte_size);
        } else {
          auto bidding_signals = std::make_unique<BiddingSignals>();
          if (!options.bidding_signals_value.empty()) {
            bidding_signals->trusted_signals =
                std::make_unique<std::string>(options.bidding_signals_value);
            bidding_signals->is_hybrid_v1_return = options.is_hybrid_v1_return;
          }
          std::move(on_done)(std::move(bidding_signals), get_byte_size);
        }
      };
  if (options.match_any_params_any_times) {
    EXPECT_CALL(provider, Get(_, _, _, _))
        .Times(AnyNumber())
        .WillOnce(MockBiddingSignalsProvider);
  } else if (options.repeated_get_allowed) {
    EXPECT_CALL(provider,
                Get(An<const BiddingSignalsRequest&>(),
                    An<absl::AnyInvocable<
                        void(absl::StatusOr<std::unique_ptr<BiddingSignals>>,
                             GetByteSize) &&>>(),
                    An<absl::Duration>(), _))
        .WillRepeatedly(MockBiddingSignalsProvider);
  } else {
    EXPECT_CALL(provider,
                Get(An<const BiddingSignalsRequest&>(),
                    An<absl::AnyInvocable<
                        void(absl::StatusOr<std::unique_ptr<BiddingSignals>>,
                             GetByteSize) &&>>(),
                    An<absl::Duration>(), _))
        .WillOnce(MockBiddingSignalsProvider);
  }
}

std::unique_ptr<MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>
SetupBiddingProviderMock(const BiddingProviderMockOptions& options) {
  auto provider = std::make_unique<
      MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>();
  // De-referencing the uPtr gives us the object. We can take a reference of it
  // since we know the object will outlive its reference and will not be moved
  // or modified while this method runs.
  SetupBiddingProviderMock(*provider, options);
  return provider;
}

void SetupBiddingProviderMockV2(
    KVAsyncClientMock* kv_async_client,
    const kv_server::v2::GetValuesResponse& response) {
  EXPECT_CALL(
      *kv_async_client,
      ExecuteInternal(
          An<std::unique_ptr<GetValuesRequest>>(), An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce(
          [response](std::unique_ptr<GetValuesRequest> get_values_raw_request,
                     grpc::ClientContext* context, auto on_done,
                     absl::Duration timeout, RequestConfig request_config) {
            std::move(on_done)(std::make_unique<GetValuesResponse>(response),
                               /* response_metadata= */ {});
            return absl::OkStatus();
          });
}

void SetupBiddingProviderMockHybrid(
    KVAsyncClientMock* kv_async_client,
    const kv_server::v2::GetValuesResponse& response,
    const std::string& v1_byos_output) {
  EXPECT_CALL(
      *kv_async_client,
      ExecuteInternal(
          An<std::unique_ptr<GetValuesRequest>>(), An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([v1_byos_output, response](
                    std::unique_ptr<GetValuesRequest> get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::string byos_output_passed_to_v2_request =
            get_values_raw_request->metadata()
                .fields()
                .at(kByosOutput)
                .string_value();
        EXPECT_EQ(v1_byos_output, byos_output_passed_to_v2_request);
        std::move(on_done)(std::make_unique<GetValuesResponse>(response),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      });
}

}  // namespace privacy_sandbox::bidding_auction_servers
