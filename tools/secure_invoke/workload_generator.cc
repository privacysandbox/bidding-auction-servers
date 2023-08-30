#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>

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

ABSL_FLAG(int, num_threads, 4,
          "Number of threads.");

ABSL_FLAG(int, num_calls_per_thread, 10000,
          "Number of calls per thread.");

ABSL_FLAG(int, bin_size, 10000,
          "size of each bucket.");

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
  if (target_service == kSfe) {
    CHECK(input_format == kJsonFormat)
        << "Input request to be sent to SFE is acceptable only in "
        << kJsonFormat;
  }
  CHECK(!op.empty())
      << "Please specify the operation to be performed - encrypt/invoke. This "
         "tool can only be used to call B&A servers running in test mode.";
  int num_threads = absl::GetFlag(FLAGS_num_threads);
  int num_calls_per_thread = absl::GetFlag(FLAGS_num_calls_per_thread);
  int bin_size = absl::GetFlag(FLAGS_bin_size);
  
  if (op == "encrypt") {
    CHECK(target_service == kSfe)
        << "Encrypt option currently only supported for SFE";
    json_input_str =
        privacy_sandbox::bidding_auction_servers::LoadFile(input_file);
    // LOG causes clipping of response.
    std::cout << privacy_sandbox::bidding_auction_servers::
            PackagePlainTextSelectAdRequestToJson(json_input_str, client_type);
  } else if (op == "invoke") {
    std::vector<std::thread> threads(num_threads);
    int num_bins = 10; int bin_size = bin_size;
    int bins[num_threads][num_bins];
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_threads; ++i) {
        for (int j = 0; j < num_bins; j++) {
            bins[i][j] = 0;
        }
        threads[i] = std::thread([=, &bins] {
            for (int j = 0; j < num_calls_per_thread; ++j) {
                auto t1 = std::chrono::high_resolution_clock::now();

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

                auto t2 = std::chrono::high_resolution_clock::now();
                auto time = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
                int bin = static_cast<int>(time / bin_size);
                if (bin < num_bins - 1)
                    ++bins[i][bin];
                else 
                    ++bins[i][num_bins-1];
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Elapsed time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << "ms\n";
    for (int i = 0; i < num_bins; ++i) {
        int count = 0;
        for (int j = 0; j < num_threads; j++) {
            count += bins[j][i];
        }
        std::cout << "[" << i * bin_size << ", " << (i+1)*bin_size << "): " << count << "\n";
    }

  } else {
    LOG(FATAL) << "Unsupported operation.";
  }
  return 0;
}

