//  Copyright 2025 Google LLC
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

#include <grpcpp/grpcpp.h>

#include "absl/status/status.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "src/util/status_macro/status_macros.h"

#include "parc_parameter_client.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::Status MaybeInitConfigClient(bool try_init,
                                   TrustedServersConfigClient& config_client,
                                   absl::string_view config_param_prefix) {
  if (!try_init) {
    return absl::OkStatus();
  }
  auto channel_args = grpc::ChannelArguments();
  // Set max receive message size to 100 MiB
  channel_args.SetMaxReceiveMessageSize(100 * 1 << 20);
  auto channel = grpc::CreateCustomChannel(
      config_client.GetStringParameter(PARC_ADDR).data(),
      grpc::InsecureChannelCredentials(), channel_args);

  PS_RETURN_IF_ERROR(
      config_client.Init(
          config_param_prefix,
          std::make_unique<ParcParameterClient>(
              privacysandbox::apis::parc::v0::ParcService::NewStub(channel))))
          .LogError()
      << "Parc Config client failed to initialize.";
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
