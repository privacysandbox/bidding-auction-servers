/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/common/data_fetch/periodic_bucket_code_fetcher.h"

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/time.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"
#include "src/core/interface/async_context.h"
#include "src/core/interface/errors.h"
#include "src/public/core/interface/execution_result.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/proto/blob_storage_service/v1/blob_storage_service.pb.h"
#include "src/util/status_macro/status_macros.h"

using ::google::cmrt::sdk::blob_storage_service::v1::BlobMetadata;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::errors::GetErrorMessage;
using ::google::scp::cpio::BlobStorageClientFactory;
using ::google::scp::cpio::BlobStorageClientInterface;

namespace privacy_sandbox::bidding_auction_servers {

PeriodicBucketCodeFetcher::PeriodicBucketCodeFetcher(
    absl::string_view bucket_name, absl::Duration fetch_period_ms,
    UdfCodeLoaderInterface* loader, server_common::Executor* executor,
    WrapCodeForDispatch wrap_code,
    BlobStorageClientInterface* blob_storage_client)
    : PeriodicBucketFetcher(bucket_name, fetch_period_ms, executor,
                            blob_storage_client),
      wrap_code_(std::move(wrap_code)),
      loader_(*loader) {}

bool PeriodicBucketCodeFetcher::OnFetch(
    const AsyncContext<GetBlobRequest, GetBlobResponse>& context) {
  const std::string version = context.request->blob_metadata().blob_name();
  if (!context.result.Successful()) {
    PS_LOG(ERROR, SystemLogContext())
        << "Failed to fetch blob: " << version
        << GetErrorMessage(context.result.status_code);
    return false;
  }
  auto result_value = {context.response->blob().data()};
  std::string wrapped_code = wrap_code_(result_value);
  // Construct the success log message before calling LoadSync so that we can
  // move the code.
  std::string success_log_message =
      absl::StrCat("Current code loaded into Roma for version ", version, ":\n",
                   wrapped_code);
  absl::Status roma_result = loader_.LoadSync(version, std::move(wrapped_code));
  if (!roma_result.ok()) {
    PS_LOG(ERROR, SystemLogContext())
        << "Roma failed to load blob: " << roma_result;
    return false;
  }
  PS_VLOG(kSuccess) << success_log_message;
  return true;
}

}  // namespace privacy_sandbox::bidding_auction_servers
