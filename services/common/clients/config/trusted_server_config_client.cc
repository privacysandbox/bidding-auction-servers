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

#include "services/common/clients/config/trusted_server_config_client.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "src/core/interface/errors.h"
#include "src/public/core/interface/execution_result.h"
#include "src/public/cpio/interface/parameter_client/parameter_client_interface.h"
#include "src/util/status_macro/status_macros.h"

using ::google::cmrt::sdk::parameter_service::v1::GetParameterRequest;
using ::google::cmrt::sdk::parameter_service::v1::GetParameterResponse;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::core::errors::GetErrorMessage;
using ::google::scp::cpio::ParameterClientFactory;
using ::google::scp::cpio::ParameterClientInterface;
using ::google::scp::cpio::ParameterClientOptions;

constexpr char error_message[] =
    "GetParameter failed during parameter fetch (parameter_name: %s, "
    "status_code: %s)\n";

absl::Status HandleFailure(absl::string_view error) noexcept {
  ABSL_LOG(ERROR) << error;
  return absl::UnavailableError(error);
}

namespace privacy_sandbox::bidding_auction_servers {
TrustedServersConfigClient::TrustedServersConfigClient(
    absl::Span<const absl::string_view> all_flags,
    absl::AnyInvocable<
        std::unique_ptr<ParameterClientInterface>(ParameterClientOptions) &&>
        config_client_provider_fn)
    : config_client_provider_fn_(std::move(config_client_provider_fn)) {
  for (absl::string_view flag_name : all_flags) {
    config_entries_map_.try_emplace(flag_name, kEmptyValue);
  }
}

absl::Status TrustedServersConfigClient::Init(
    std::string_view config_param_prefix) noexcept {
  // Initialize and run the config client to fetch the corresponding values for
  // empty_parameter.
  config_client_ =
      std::move(config_client_provider_fn_)(ParameterClientOptions());
  PS_RETURN_IF_ERROR(InitAndRunConfigClient());

  // Add the fetched values back to config_entries_map_.
  absl::BlockingCounter counter(config_entries_map_.size());
  for (const auto& [key, value] : config_entries_map_) {
    GetParameterRequest get_parameter_request;
    get_parameter_request.set_parameter_name(
        absl::StrCat(config_param_prefix, key));

    // Callbacks occur synchronously.
    // The GetParameter() call returns success given a valid request object
    // (e.g. a non-empty parameter name).
    absl::Status status = config_client_->GetParameter(
        std::move(get_parameter_request),
        [this, &key = key, &initial_value = value, &counter](
            const ExecutionResult& result,
            const GetParameterResponse& response) {
          if (result.Successful()) {
            config_entries_map_.insert_or_assign(key,
                                                 response.parameter_value());
          } else if (initial_value == kEmptyValue) {
            ABSL_LOG(ERROR) << absl::StrFormat(
                error_message, key, GetErrorMessage(result.status_code));
          } else {
            ABSL_LOG(WARNING) << absl::StrFormat(
                error_message, key, GetErrorMessage(result.status_code));
          }
          counter.DecrementCount();
        });

    // Throw an error if key value not set before this and not found in
    // parameter store.
    if (!status.ok() && value == kEmptyValue) {
      return HandleFailure(
          absl::StrFormat(error_message, key, status.message()));
    }
  }

  counter.Wait();
  return absl::OkStatus();
}

bool TrustedServersConfigClient::HasParameter(
    absl::string_view name) const noexcept {
  return config_entries_map_.contains(name);
}

absl::string_view TrustedServersConfigClient::GetStringParameter(
    absl::string_view name) const noexcept {
  DCHECK(HasParameter(name)) << "Flag " << name << " not found";
  return config_entries_map_.at(name);
}

bool TrustedServersConfigClient::GetBooleanParameter(
    absl::string_view name) const noexcept {
  DCHECK(HasParameter(name)) << "Flag " << name << " not found";
  return absl::AsciiStrToLower(config_entries_map_.at(name)) == kTrue;
}

int TrustedServersConfigClient::GetIntParameter(
    absl::string_view name) const noexcept {
  DCHECK(HasParameter(name)) << "Flag " << name << " not found";
  return std::stoi(config_entries_map_.at(name));
}

int64_t TrustedServersConfigClient::GetInt64Parameter(
    absl::string_view name) const noexcept {
  DCHECK(HasParameter(name)) << "Flag " << name << " not found";
  return std::stol(config_entries_map_.at(name));
}

absl::Status TrustedServersConfigClient::InitAndRunConfigClient() noexcept {
  PS_RETURN_IF_ERROR(config_client_->Init()).SetPrepend()
      << "Cannot init config client: ";
  PS_RETURN_IF_ERROR(config_client_->Run()).SetPrepend()
      << "Cannot run config client";
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
