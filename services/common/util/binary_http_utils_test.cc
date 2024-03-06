// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/util/binary_http_utils.h"

#include <google/protobuf/struct.pb.h>

#include "absl/log/check.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/text_format.h"
#include "include/gtest/gtest.h"
#include "public/query/v2/get_values_v2.grpc.pb.h"
#include "public/query/v2/get_values_v2.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using kv_server::v2::GetValuesRequest;
using kv_server::v2::GetValuesResponse;

inline constexpr char kTestArgumentValue[] = "test_data";

std::string TestGetValuesRequest() {
  return absl::Substitute(
      R"pb(
        partitions { arguments { data { string_value: "$0" } } }
      )pb",
      kTestArgumentValue);
}

TEST(BinaryHttpRequest, CreatesRequestWithJsonPayload) {
  GetValuesRequest get_values_request;
  CHECK(
      TextFormat::ParseFromString(TestGetValuesRequest(), &get_values_request))
      << "Unable to parse text proto";
  auto binary_http_request = ToBinaryHTTP(get_values_request, /*to_json=*/true);
  CHECK_OK(binary_http_request);

  EXPECT_EQ(absl::BytesToHexString(*binary_http_request),
            "00000000001e0c636f6e74656e742d74797065106170706c69636174696f6e2f6a"
            "736f6e357b22706172746974696f6e73223a5b7b22617267756d656e7473223a5b"
            "7b2264617461223a22746573745f64617461227d5d7d5d7d");
}
TEST(BinaryHttpRequest, CreatesRequestWithProtoPayload) {
  GetValuesRequest get_values_request;
  CHECK(
      TextFormat::ParseFromString(TestGetValuesRequest(), &get_values_request))
      << "Unable to parse text proto";
  auto binary_http_request =
      ToBinaryHTTP(get_values_request, /*to_json=*/false);
  CHECK_OK(binary_http_request);

  EXPECT_EQ(absl::BytesToHexString(*binary_http_request),
            "0000000000220c636f6e74656e742d74797065146170706c69636174696f6e2f70"
            "726f746f627566111a0f2a0d120b1a09746573745f64617461");
}

TEST(BinaryHttpResponse, RetrievesResponseFromJsonPayload) {
  std::string test_binary_http_response_hex =
      "0140c8003d7b2273696e676c65506172746974696f6e223a7b22737472696e674f757470"
      "7574223a225b7b5c2263617465676f72795c223a5c22305c227d5d227d7d";
  auto get_values_response = FromBinaryHTTP<GetValuesResponse>(
      absl::HexStringToBytes(test_binary_http_response_hex),
      /*from_json=*/true);
  CHECK_OK(get_values_response);

  ASSERT_TRUE(get_values_response->has_single_partition());
  ASSERT_TRUE(get_values_response->single_partition().has_string_output());
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
