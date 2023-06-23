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

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.pb.h"
#include "gtest/gtest.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kHandleName = "GetVersion";

DispatchConfig config;

std::string BuildCodeToLoad(int version) {
  return absl::StrCat("function ", kHandleName, "() { return { test: \"",
                      version, "\"}; }");
}

std::vector<DispatchRequest> BuildRequests(int request_count, int version_num) {
  std::vector<DispatchRequest> requests;
  for (int i = 0; i < request_count; i++) {
    DispatchRequest request;
    request.id = std::to_string(i);
    request.handler_name = kHandleName;
    request.version_num = version_num;
    requests.push_back(std::move(request));
  }
  return requests;
}

void CheckResponses(
    const std::vector<absl::StatusOr<DispatchResponse>>& results,
    absl::BlockingCounter& count, absl::string_view expected_response) {
  for (auto& res : results) {
    EXPECT_TRUE(res.ok());
    google::protobuf::Struct output;
    EXPECT_TRUE(
        google::protobuf::util::JsonStringToMessage(res.value().resp, &output)
            .ok());
    EXPECT_EQ(output.fields().at("test").string_value(), expected_response);
    count.DecrementCount();
  }
}

TEST(CodeDispatchClientTest, PassesJavascriptResultToCallback) {
  V8Dispatcher dispatcher;
  DispatchConfig config;
  ASSERT_TRUE(dispatcher.Init(config).ok());
  const int version = 1;
  ASSERT_TRUE(dispatcher.LoadSync(version, BuildCodeToLoad(version)).ok());
  CodeDispatchClient client(dispatcher);
  int request_count = 100;
  std::vector<DispatchRequest> requests = BuildRequests(request_count, version);

  absl::BlockingCounter done(request_count);
  auto status = client.BatchExecute(
      requests,
      [&done](const std::vector<absl::StatusOr<DispatchResponse>>& results) {
        CheckResponses(results, done, std::to_string(version));
      });

  ASSERT_TRUE(status.ok());
  done.Wait();
  ASSERT_TRUE(dispatcher.Stop().ok());
}

TEST(CodeDispatchClientTest, RunsLatestCodeVersion) {
  V8Dispatcher dispatcher;
  DispatchConfig config;
  ASSERT_TRUE(dispatcher.Init(config).ok());
  CodeDispatchClient client(dispatcher);

  int version_count = 3;
  int request_count = 10;
  for (int current_version = 1; current_version <= version_count;
       current_version++) {
    ASSERT_TRUE(
        dispatcher.LoadSync(current_version, BuildCodeToLoad(current_version))
            .ok());
    std::vector<DispatchRequest> requests =
        BuildRequests(request_count, current_version);
    absl::BlockingCounter done(request_count);
    auto status = client.BatchExecute(
        requests,
        [&done, current_version](
            const std::vector<absl::StatusOr<DispatchResponse>>& results) {
          CheckResponses(results, done, std::to_string(current_version));
        });

    ASSERT_TRUE(status.ok());
    done.Wait();
  }
  ASSERT_TRUE(dispatcher.Stop().ok());
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
