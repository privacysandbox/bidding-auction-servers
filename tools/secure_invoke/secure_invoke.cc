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
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/ascii.h"
#include "absl/synchronization/notification.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "tools/secure_invoke/payload_generator/payload_packaging.h"
#include "tools/secure_invoke/secure_invoke_lib.h"

constexpr absl::string_view kAndroidClientType = "ANDROID";
constexpr char kSfe[] = "SFE";
constexpr char kBfe[] = "BFE";
constexpr char kBidding[] = "BIDDING";
constexpr char kJsonFormat[] = "JSON";
constexpr char kProtoFormat[] = "PROTO";

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
  SelectAdRequest::ClientType client_type =
      absl::AsciiStrToUpper(input_client_type) == kAndroidClientType
          ? SelectAdRequest::ANDROID
          : SelectAdRequest::BROWSER;
  const std::string target_service =
      absl::AsciiStrToUpper(absl::GetFlag(FLAGS_target_service));
  const std::string input_format =
      absl::AsciiStrToUpper(absl::GetFlag(FLAGS_input_format));
  if (json_input_str.empty()) {
    CHECK(!input_file.empty()) << "Please specify full path for input request";
  }
  CHECK(input_format == kProtoFormat || input_format == kJsonFormat)
      << "Unexpected input format specified: " << input_format;
  CHECK(target_service == kSfe || target_service == kBfe || target_service == kBidding)
      << "Unsupported target service: " << target_service;
  if (target_service == kSfe) {
    CHECK(input_format == kJsonFormat)
        << "Input request to be sent to SFE is acceptable only in "
        << kJsonFormat;
  }
  CHECK(!op.empty())
      << "Please specify the operation to be performed - encrypt/invoke. This "
         "tool can only be used to call B&A servers running in test mode.";
  if (op == "encrypt") {
    if (target_service == kSfe) {
      json_input_str =
          privacy_sandbox::bidding_auction_servers::LoadFile(input_file);
      // LOG causes clipping of response.
      std::cout << privacy_sandbox::bidding_auction_servers::
              PackagePlainTextSelectAdRequestToJson(json_input_str,
                                                    client_type);
    } else {
      std::cout << privacy_sandbox::bidding_auction_servers::
              PackagePlainTextGetBidsRequestToJson();
    }
  } else if (op == "invoke") {
    if (target_service == kSfe) {
      const auto status =
          privacy_sandbox::bidding_auction_servers::SendRequestToSfe(
              client_type);
      CHECK(status.ok()) << status;
    } else if (target_service == kBfe) {
      const auto status =
          privacy_sandbox::bidding_auction_servers::SendRequestToBfe();
      CHECK(status.ok()) << status;
    } else if (target_service == kBidding) {
      const auto status =
          privacy_sandbox::bidding_auction_servers::SendRequestToBidding();
      CHECK(status.ok()) << status;
    } else {
      LOG(FATAL) << "Unsupported target service: " << target_service;
    }
  } else {
    LOG(FATAL) << "Unsupported operation.";
  }
  return 0;
}
