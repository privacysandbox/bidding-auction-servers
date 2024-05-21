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

#include "services/common/util/oblivious_http_utils.h"

#include <google/protobuf/struct.pb.h>

#include "absl/log/check.h"
#include "absl/strings/escaping.h"
#include "include/gtest/gtest.h"
#include "public/constants.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using kv_server::kAEADParameter;
using kv_server::kKDFParameter;
using kv_server::kKEMParameter;

constexpr char kTestClientRequestData[] = "test_client_data";
constexpr char kTestServerResponseData[] = "test_server_data";

absl::StatusOr<quiche::ObliviousHttpGateway> CreateTestOHTTPGateway() {
  PS_ASSIGN_OR_RETURN(auto config, quiche::ObliviousHttpHeaderKeyConfig::Create(
                                       kTestKeyId, kKEMParameter, kKDFParameter,
                                       kAEADParameter));
  return quiche::ObliviousHttpGateway::Create(
      absl::HexStringToBytes(kTestPrivateKey), config);
}

absl::StatusOr<quiche::ObliviousHttpRequest> GetDecryptedRequest(
    absl::string_view request) {
  PS_ASSIGN_OR_RETURN(auto gateway, CreateTestOHTTPGateway());
  PS_ASSIGN_OR_RETURN(auto decrypted_request,
                      gateway.DecryptObliviousHttpRequest(request));
  return decrypted_request;
}

TEST(ObliviousHttp, RequestResponse) {
  // Test request path.
  std::string client_request_data = kTestClientRequestData;
  auto oblivious_http_request = ToObliviousHTTPRequest(
      client_request_data, absl::HexStringToBytes(kTestPublicKey), kTestKeyId,
      kKEMParameter, kKDFParameter, kAEADParameter);
  ASSERT_TRUE(oblivious_http_request.ok()) << oblivious_http_request.status();
  std::string encrypted_request =
      oblivious_http_request->EncapsulateAndSerialize();
  auto decrypted_request = GetDecryptedRequest(encrypted_request);
  ASSERT_TRUE(decrypted_request.ok()) << decrypted_request.status();
  EXPECT_EQ(decrypted_request->GetPlaintextData(), kTestClientRequestData);

  // Test response path.
  auto gateway = CreateTestOHTTPGateway();
  ASSERT_TRUE(gateway.ok()) << gateway.status();
  auto server_context = std::move(*decrypted_request).ReleaseContext();
  auto encapsulate_response = gateway->CreateObliviousHttpResponse(
      kTestServerResponseData, server_context);
  ASSERT_TRUE(encapsulate_response.ok()) << encapsulate_response.status();
  auto client_context = std::move(*oblivious_http_request).ReleaseContext();
  std::string serialized_response =
      encapsulate_response->EncapsulateAndSerialize();
  auto plain_text_response =
      FromObliviousHTTPResponse(serialized_response, client_context);
  ASSERT_TRUE(plain_text_response.ok()) << plain_text_response.status();
  EXPECT_EQ(*plain_text_response, kTestServerResponseData);
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
