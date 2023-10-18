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
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/synchronization/notification.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_utils.h"
#include "tools/secure_invoke/payload_generator/payload_packaging.h"
#include "tools/secure_invoke/secure_invoke_lib.h"

constexpr absl::string_view kAndroidClientType = "CLIENT_TYPE_ANDROID";
constexpr char kSfe[] = "SFE";
constexpr char kBfe[] = "BFE";
constexpr char kJsonFormat[] = "JSON";
constexpr char kProtoFormat[] = "PROTO";

using ::privacy_sandbox::bidding_auction_servers::ClientType;
using ::privacy_sandbox::bidding_auction_servers::SelectAdRequest;

ABSL_FLAG(
    std::string, input_file, "",
    "Complete path to the file containing unencrypted request"
    "The file may contain JSON or protobuf based GetBidsRequest payload.");

ABSL_FLAG(std::string, input_format, kJsonFormat,
          "Format of request in the input file. Valid values: JSON, PROTO "
          "(Note: for SelectAdRequest to BFE only JSON is supported)");

ABSL_FLAG(std::string, json_input_str, "",
          "The unencrypted JSON request to be used. If provided, this will be "
          "used to send request rather than loading it from an input file");

ABSL_FLAG(std::string, op, "",
          "The operation to be performed - invoke/encrypt.");

ABSL_FLAG(std::string, host_addr, "",
          "The Address for the SellerFrontEnd server to be invoked.");

ABSL_FLAG(std::string, client_ip, "",
          "The IP for the B&A client to be forwarded to KV servers.");

ABSL_FLAG(std::string, client_accept_language, "en-US,en;q=0.9",
          "The accept-language header for the B&A client to be forwarded to KV "
          "servers.");

ABSL_FLAG(
    std::string, client_user_agent,
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36",
    "The user-agent header for the B&A client to be forwarded to KV servers.");

ABSL_FLAG(std::string, client_type, "",
          "Client type (browser or android) to use for request (defaults to "
          "browser)");

ABSL_FLAG(bool, insecure, false,
          "Set to true to send a request to an SFE server running with "
          "insecure credentials");

ABSL_FLAG(std::string, target_service, kSfe,
          "Service name to which the request must be sent to.");

// Coordinator key defaults correspond to defaults in
// https://github.com/privacysandbox/data-plane-shared-libraries/blob/e293c1bdd52e3cf3c0735cd182183eeb8ebf032d/src/cpp/encryption/key_fetcher/src/fake_key_fetcher_manager.h#L29C34-L29C34
ABSL_FLAG(std::string, public_key,
          "87ey8XZPXAd+/+ytKv2GFUWW5j9zdepSJ2G4gebDwyM=",
          "Use exact output from the coordinator. Public key. Must be base64.");
ABSL_FLAG(std::string, key_id, "4000000000000000",
          "Use exact output from the coordinator. Hexadecimal key id string "
          "with trailing zeros.");

namespace {}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Set log level to 0 and to be shown on terminal.
  google::LogToStderr();
  google::SetStderrLogging(google::GLOG_INFO);
  google::InitGoogleLogging(argv[0]);
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

  std::string id =
      privacy_sandbox::server_common::ToOhttpKeyId(absl::GetFlag(FLAGS_key_id));
  uint8_t key_id = stoi(id);

  if (op == "encrypt") {
    if (target_service == kSfe) {
      json_input_str =
          privacy_sandbox::bidding_auction_servers::LoadFile(input_file);
      // LOG causes clipping of response.
      std::cout << privacy_sandbox::bidding_auction_servers::
              PackagePlainTextSelectAdRequestToJson(json_input_str, client_type,
                                                    public_key_hex, key_id);
    } else {
      std::cout << privacy_sandbox::bidding_auction_servers::
              PackagePlainTextGetBidsRequestToJson(public_key_hex, key_id);
    }
  } else if (op == "invoke") {
    if (target_service == kSfe) {
      const auto status =
          privacy_sandbox::bidding_auction_servers::SendRequestToSfe(
              client_type, public_key_hex, key_id);
      CHECK(status.ok()) << status;
    } else if (target_service == kBfe) {
      const auto status =
          privacy_sandbox::bidding_auction_servers::SendRequestToBfe(
              public_key_hex, key_id);
      CHECK(status.ok()) << status;
    } else {
      LOG(FATAL) << "Unsupported target service: " << target_service;
    }
  } else {
    LOG(FATAL) << "Unsupported operation.";
  }
  return 0;
}
