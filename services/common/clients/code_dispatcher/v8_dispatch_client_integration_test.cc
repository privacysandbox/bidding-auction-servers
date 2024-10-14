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
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kHandleName = "GetVersion";

DispatchConfig config;

std::string BuildCodeToLoad(absl::string_view version) {
  return absl::StrCat("function ", kHandleName, "() { return { test: \"",
                      version, "\"}; }");
}

std::vector<DispatchRequest> BuildRequests(int request_count,
                                           const std::string& version) {
  std::vector<DispatchRequest> requests;
  for (int i = 0; i < request_count; i++) {
    DispatchRequest request;
    request.id = std::to_string(i);
    request.handler_name = kHandleName;
    request.version_string = version;
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

class V8DispatchClientTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(V8DispatchClientTest, PassesJavascriptResultToCallback) {
  V8Dispatcher dispatcher;
  ASSERT_TRUE(dispatcher.Init().ok());
  const std::string version = "v1";
  ASSERT_TRUE(dispatcher.LoadSync(version, BuildCodeToLoad(version)).ok());
  V8DispatchClient client(dispatcher);
  int request_count = 100;
  std::vector<DispatchRequest> requests = BuildRequests(request_count, version);

  absl::BlockingCounter done(request_count);
  auto status = client.BatchExecute(
      requests,
      [&done,
       &version](const std::vector<absl::StatusOr<DispatchResponse>>& results) {
        CheckResponses(results, done, version);
      });

  ASSERT_TRUE(status.ok());
  done.Wait();
}

TEST_F(V8DispatchClientTest, RunsLatestCodeVersion) {
  V8Dispatcher dispatcher;
  ASSERT_TRUE(dispatcher.Init().ok());
  V8DispatchClient client(dispatcher);

  int version_count = 3;
  int request_count = 10;
  for (int current_version = 1; current_version <= version_count;
       current_version++) {
    std::string version_str = absl::StrCat("v", current_version);

    ASSERT_TRUE(
        dispatcher.LoadSync(version_str, BuildCodeToLoad(version_str)).ok());
    std::vector<DispatchRequest> requests =
        BuildRequests(request_count, version_str);
    absl::BlockingCounter done(request_count);
    auto status = client.BatchExecute(
        requests,
        [&done, version_str](
            const std::vector<absl::StatusOr<DispatchResponse>>& results) {
          CheckResponses(results, done, version_str);
        });

    ASSERT_TRUE(status.ok());
    done.Wait();
  }
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
