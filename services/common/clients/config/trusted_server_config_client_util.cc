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

#include "services/common/clients/config/trusted_server_config_client_util.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "src/public/core/interface/execution_result.h"
#include "src/public/cpio/interface/instance_client/instance_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

using google::cmrt::sdk::instance_service::v1::
    GetCurrentInstanceResourceNameRequest;
using google::cmrt::sdk::instance_service::v1::
    GetCurrentInstanceResourceNameResponse;
using google::cmrt::sdk::instance_service::v1::
    GetInstanceDetailsByResourceNameRequest;
using google::cmrt::sdk::instance_service::v1::
    GetInstanceDetailsByResourceNameResponse;
using google::cmrt::sdk::instance_service::v1::GetTagsByResourceNameRequest;
using google::cmrt::sdk::instance_service::v1::GetTagsByResourceNameResponse;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::errors::GetErrorMessage;
using ::google::scp::cpio::InstanceClientInterface;

namespace {

// Keys for EC2 tags.
inline constexpr char kOperatorTagName[] = "operator";
inline constexpr char kEnvironmentTagName[] = "environment";
inline constexpr char kServiceTagName[] = "service";

inline constexpr char kResourceNameFetchError[] =
    "Unable to fetch instance resource name: (status_code: %s)";
inline constexpr char kResourceTagFetchError[] =
    "Unable to fetch instance's tags: (status_code: %s)";

absl::Status HandleFailure(absl::string_view error) noexcept {
  ABSL_LOG(ERROR) << error;
  return absl::InternalError(error);
}

absl::StatusOr<std::string> GetResourceName(
    std::shared_ptr<InstanceClientInterface> client) {  // NOLINT
  std::string resource_name;

  absl::Notification done;
  absl::Status status = client->GetCurrentInstanceResourceName(
      GetCurrentInstanceResourceNameRequest(),
      [&resource_name, &done](
          const ExecutionResult& result,
          const GetCurrentInstanceResourceNameResponse& response) {
        if (result.Successful()) {
          resource_name = std::string{response.instance_resource_name()};
        } else {
          ABSL_LOG(ERROR) << absl::StrFormat(
              kResourceNameFetchError, GetErrorMessage(result.status_code));
        }

        done.Notify();
      });

  if (!status.ok()) {
    return HandleFailure(
        absl::StrFormat(kResourceNameFetchError, status.message()));
  }

  done.WaitForNotification();

  if (resource_name.empty()) {
    return absl::InternalError("Could not fetch instance resource name.");
  }

  return resource_name;
}

}  // namespace

TrustedServerConfigUtil::TrustedServerConfigUtil(bool init_config_client)
    : init_config_client_(init_config_client) {
  if (!init_config_client_) {
    return;
  }

  std::shared_ptr<InstanceClientInterface> client =
      google::scp::cpio::InstanceClientFactory::Create();
  client->Init().IgnoreError();
  absl::StatusOr<std::string> resource_name = GetResourceName(client);
  CHECK_OK(resource_name) << "Could not fetch host resource name.";
  ComputeZone(resource_name.value());
  absl::Notification done;
  GetInstanceDetailsByResourceNameRequest request;
  request.set_instance_resource_name(resource_name.value());

  absl::Status status = client->GetInstanceDetailsByResourceName(
      std::move(request),
      [this, &done](const ExecutionResult& result,
                    const GetInstanceDetailsByResourceNameResponse& response) {
        if (result.Successful()) {
          ABSL_LOG(INFO) << response.DebugString();
          instance_id_ = std::string{response.instance_details().instance_id()};
          operator_ = response.instance_details().labels().at(kOperatorTagName);
          environment_ =
              response.instance_details().labels().at(kEnvironmentTagName);
          service_ = response.instance_details().labels().at(kServiceTagName);
        } else {
          ABSL_LOG(ERROR) << absl::StrFormat(
              kResourceTagFetchError, GetErrorMessage(result.status_code));
        }
        done.Notify();
      });
  if (!status.ok()) {
    ABSL_LOG(ERROR) << absl::StrFormat(kResourceTagFetchError,
                                       status.message());
  } else {
    done.WaitForNotification();
  }
}

// Returns the string to prepend the names of all keys/flags fetched from the
// Parameter Store. The prepended string follows the format
// "{operator}-{environment}-" where {operator} and {environment} are values for
// the EC2 tag keys by these names.
std::string TrustedServerConfigUtil::GetConfigParameterPrefix() noexcept {
  return absl::StrCat(operator_, "-", environment_, "-");
}

}  // namespace privacy_sandbox::bidding_auction_servers
