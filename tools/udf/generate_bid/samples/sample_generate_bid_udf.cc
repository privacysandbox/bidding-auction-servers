
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

#include "api/udf/generate_bid_udf_interface.pb.h"
#include "google/protobuf/util/delimited_message_util.h"

using ::google::protobuf::io::FileInputStream;
using ::google::protobuf::util::ParseDelimitedFromZeroCopyStream;
using ::google::protobuf::util::SerializeDelimitedToFileDescriptor;
using ::privacy_sandbox::bidding_auction_servers::roma_service::
    GenerateProtectedAudienceBidRequest;
using ::privacy_sandbox::bidding_auction_servers::roma_service::
    GenerateProtectedAudienceBidResponse;

GenerateProtectedAudienceBidRequest ReadRequestFromFd(int fd) {
  GenerateProtectedAudienceBidRequest req;
  FileInputStream stream(fd);
  google::protobuf::util::ParseDelimitedFromZeroCopyStream(&req, &stream,
                                                           nullptr);
  return req;
}

void WriteResponseToFd(int fd,
                       const GenerateProtectedAudienceBidResponse& resp) {
  google::protobuf::util::SerializeDelimitedToFileDescriptor(resp, fd);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Not enough arguments!" << '\n';
    return -1;
  }
  int fd = std::stoi(argv[1]);
  GenerateProtectedAudienceBidRequest request = ReadRequestFromFd(fd);
  GenerateProtectedAudienceBidResponse response;
  auto bid = response.add_bids();
  bid->set_ad("ad");
  bid->set_bid(1.0);
  bid->set_render("https://my-render-url");
  bid->add_ad_components("https://my-ad-component");
  bid->set_ad_cost(2.0);
  bid->set_modeling_signals(3);
  bid->set_bid_currency("USD");
  bid->mutable_debug_report_urls()->set_auction_debug_win_url(
      "https://my-debug-url/win");
  bid->mutable_debug_report_urls()->set_auction_debug_loss_url(
      "https://my-debug-url/loss");
  response.mutable_log_messages()->add_logs(
      absl::StrCat("Generated bid of ", response.bids(0).bid()));
  WriteResponseToFd(fd, response);
  return 0;
}
