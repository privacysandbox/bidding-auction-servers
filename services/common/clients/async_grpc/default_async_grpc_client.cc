//  Copyright 2024 Google LLC
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

#include "services/common/clients/async_grpc/default_async_grpc_client.h"

#include <memory>

#include "services/common/util/file_util.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

grpc::SslCredentialsOptions GetDefaultSslCredentials(
    absl::string_view cert_path) {
  absl::StatusOr<std::string> roots_pem = GetFileContent(cert_path);
  PS_CHECK_OK(roots_pem, SystemLogContext())
      << "Unable to read from: " << cert_path;
  return {
      .pem_root_certs = *std::move(roots_pem),
  };
}

}  // namespace

std::shared_ptr<grpc::Channel> CreateChannel(
    absl::string_view server_addr, bool compression, bool secure,
    absl::string_view grpc_arg_default_authority, absl::string_view ca_cert) {
  PS_VLOG(5) << "Creating " << (secure ? "secure" : "insecure")
             << " credentials " << (secure ? "with cert: " : "without cert: ")
             << ca_cert;
  std::shared_ptr<grpc::ChannelCredentials> creds =
      secure ? grpc::SslCredentials(GetDefaultSslCredentials(ca_cert))
             : grpc::InsecureChannelCredentials();
  grpc::ChannelArguments args;
  // Set max message size to 256 MB.
  args.SetMaxSendMessageSize(256L * 1024L * 1024L);
  args.SetMaxReceiveMessageSize(256L * 1024L * 1024L);
  if (!grpc_arg_default_authority.empty() &&
      grpc_arg_default_authority != kIgnoredPlaceholderValue) {
    args.SetString(GRPC_ARG_DEFAULT_AUTHORITY,
                   grpc_arg_default_authority.data());
  }
  if (compression) {
    // Set the default compression algorithm for the channel.
    args.SetCompressionAlgorithm(GRPC_COMPRESS_GZIP);
  }
  return grpc::CreateCustomChannel(server_addr.data(), creds, args);
}

}  // namespace privacy_sandbox::bidding_auction_servers
