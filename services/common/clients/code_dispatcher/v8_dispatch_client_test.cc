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

#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"

#include <utility>

#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(V8DispatchClient, DelegatesToCodeDispatchLibrary) {
  CommonTestInit();
  MockV8Dispatcher dispatcher;
  DispatchRequest foo{"foo"};
  DispatchRequest bar{"bar"};
  std::vector<DispatchRequest> requests{foo, bar};

  absl::BlockingCounter done(1);
  BatchDispatchDoneCallback callback =
      [&done](const std::vector<absl::StatusOr<DispatchResponse>>& res) {
        done.DecrementCount();
      };
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([&foo, &bar](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback batch_callback) {
        EXPECT_EQ(batch.at(0).id, foo.id);
        EXPECT_EQ(batch.at(1).id, bar.id);
        batch_callback({});
        return absl::OkStatus();
      });
  V8DispatchClient client(dispatcher);
  EXPECT_TRUE(client.BatchExecute(requests, std::move(callback)).ok());
  done.Wait();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
