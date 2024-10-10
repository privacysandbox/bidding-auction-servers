// Copyright 2023 Google LLC
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
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/synchronization/notification.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/common/test/utils/test_init.h"
#include "src/encryption/key_fetcher/key_fetcher_utils.h"
#include "tools/secure_invoke/flags.h"
#include "tools/secure_invoke/payload_generator/payload_packaging.h"
#include "tools/secure_invoke/secure_invoke_lib.h"

constexpr absl::string_view kAndroidClientType = "CLIENT_TYPE_ANDROID";
constexpr char kSfe[] = "SFE";
constexpr char kBfe[] = "BFE";
constexpr char kJsonFormat[] = "JSON";
constexpr char kProtoFormat[] = "PROTO";

using ::privacy_sandbox::bidding_auction_servers::ClientType;
using ::privacy_sandbox::bidding_auction_servers::HpkeKeyset;
using ::privacy_sandbox::bidding_auction_servers::SelectAdRequest;

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  privacy_sandbox::bidding_auction_servers::CommonTestInit();
  // Set log level to 0 and to be shown on terminal.
  std::string input_file = absl::GetFlag(FLAGS_input_file);
  std::string json_input_str = absl::GetFlag(FLAGS_json_input_str);
  std::string op = absl::GetFlag(FLAGS_op);
  std::string input_client_type = absl::GetFlag(FLAGS_client_type);
  ClientType client_type =
      absl::AsciiStrToUpper(input_client_type) == kAndroidClientType
          ? ClientType::CLIENT_TYPE_ANDROID
          : ClientType::CLIENT_TYPE_BROWSER;
  const std::string target_service =
      absl::AsciiStrToUpper(absl::GetFlag(FLAGS_target_service));
  const std::string input_format =
      absl::AsciiStrToUpper(absl::GetFlag(FLAGS_input_format));
  if (json_input_str.empty()) {
    CHECK(!input_file.empty()) << "Please specify full path for input request";
  }
  CHECK(input_format == kProtoFormat || input_format == kJsonFormat)
      << "Unexpected input format specified: " << input_format;
  CHECK(target_service == kSfe || target_service == kBfe)
      << "Unsupported target service: " << target_service;
  if (target_service == kSfe) {
    CHECK(input_format == kJsonFormat)
        << "Input request to be sent to SFE is acceptable only in "
        << kJsonFormat;
  }
  CHECK(!op.empty())
      << "Please specify the operation to be performed - encrypt/invoke. This "
         "tool can only be used to call B&A servers running in test mode.";

  std::string public_key_bytes;
  CHECK(
      absl::Base64Unescape(absl::GetFlag(FLAGS_public_key), &public_key_bytes))
      << "Failed to unescape public key.";
  std::string public_key_hex = absl::BytesToHexString(public_key_bytes);

  std::string private_key_bytes;
  CHECK(absl::Base64Unescape(absl::GetFlag(FLAGS_private_key),
                             &private_key_bytes))
      << "Failed to unescape private key.";
  std::string private_key_hex = absl::BytesToHexString(private_key_bytes);

  std::string id =
      privacy_sandbox::server_common::ToOhttpKeyId(absl::GetFlag(FLAGS_key_id));
  const HpkeKeyset keyset = {
      .public_key = std::move(public_key_hex),
      .private_key = std::move(private_key_hex),
      .key_id = static_cast<uint8_t>(stoi(id)),
  };

  bool enable_debug_reporting = absl::GetFlag(FLAGS_enable_debug_reporting);
  std::optional<bool> enable_debug_info =
      absl::GetFlag(FLAGS_enable_debug_info);
  std::optional<bool> enable_unlimited_egress =
      absl::GetFlag(FLAGS_enable_unlimited_egress);
  if (op == "encrypt") {
    if (target_service == kSfe) {
      json_input_str =
          privacy_sandbox::bidding_auction_servers::LoadFile(input_file);
      // LOG causes clipping of response.
      std::cout << privacy_sandbox::bidding_auction_servers::
              PackagePlainTextSelectAdRequestToJson(
                  json_input_str, client_type, keyset, enable_debug_reporting,
                  enable_debug_info, enable_unlimited_egress);
    } else {
      std::cout << privacy_sandbox::bidding_auction_servers::
              PackagePlainTextGetBidsRequestToJson(
                  keyset, enable_debug_reporting, enable_unlimited_egress);
    }
  } else if (op == "invoke") {
    if (target_service == kSfe) {
      const auto status =
          privacy_sandbox::bidding_auction_servers::SendRequestToSfe(
              client_type, keyset, enable_debug_reporting, enable_debug_info,
              enable_unlimited_egress);
      CHECK(status.ok()) << status;
    } else if (target_service == kBfe) {
      const auto status =
          privacy_sandbox::bidding_auction_servers::SendRequestToBfe(
              keyset, enable_debug_reporting, /*stub=*/nullptr,
              enable_unlimited_egress);
      CHECK(status.ok()) << status;
    } else {
      LOG(FATAL) << "Unsupported target service: " << target_service;
    }
  } else {
    LOG(FATAL) << "Unsupported operation.";
  }
  return 0;
}
