/*
 * Copyright 2023 Google LLC
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

#include "services/common/code_fetch/periodic_bucket_fetcher.h"

#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "scp/cc/core/interface/async_context.h"
#include "scp/cc/core/interface/errors.h"
#include "scp/cc/public/core/interface/execution_result.h"
#include "scp/cc/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "scp/cc/public/cpio/proto/blob_storage_service/v1/blob_storage_service.pb.h"
#include "services/common/loggers/request_context_logger.h"

using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::errors::GetErrorMessage;
using ::google::scp::cpio::BlobStorageClientFactory;
using ::google::scp::cpio::BlobStorageClientInterface;

namespace privacy_sandbox::bidding_auction_servers {

PeriodicBucketFetcher::PeriodicBucketFetcher(
    absl::string_view bucket_name, absl::string_view blob_name,
    absl::Duration fetch_period_ms, V8Dispatcher& dispatcher,
    server_common::Executor* executor,
    std::unique_ptr<BlobStorageClientInterface> blob_storage_client =
        BlobStorageClientFactory::Create())
    : bucket_name_(bucket_name),
      blob_name_(blob_name),
      fetch_period_ms_(fetch_period_ms),
      dispatcher_(dispatcher),
      executor_(std::move(executor)),
      blob_storage_client_(std::move(blob_storage_client)) {}

void PeriodicBucketFetcher::Start() {
  InitAndRunConfigClient();
  executor_->Run([this]() { PeriodicBucketFetch(); });
}

void PeriodicBucketFetcher::End() {
  if (task_id_.keys != nullptr) {
    executor_->Cancel(std::move(task_id_));
  } else {
    return;
  }
}

void PeriodicBucketFetcher::InitAndRunConfigClient() {
  auto result = blob_storage_client_->Init();
  CHECK(result.Successful())
      << absl::StrFormat("Failed to init BlobStorageClient (status_code: %s)\n",
                         GetErrorMessage(result.status_code));

  result = blob_storage_client_->Run();
  CHECK(result.Successful())
      << absl::StrFormat("Failed to run BlobStorageClient (status_code: %s)\n",
                         GetErrorMessage(result.status_code));
}

void PeriodicBucketFetcher::PeriodicBucketFetch() {
  ExecutionResult result;

  auto get_blob_request = std::make_shared<GetBlobRequest>();
  get_blob_request->mutable_blob_metadata()->set_bucket_name(bucket_name_);
  get_blob_request->mutable_blob_metadata()->set_blob_name(blob_name_);

  AsyncContext<GetBlobRequest, GetBlobResponse> get_blob_context(
      move(get_blob_request), [&result, this](auto& context) {
        result = context.result;

        if (result.Successful()) {
          PS_VLOG(2) << "BlobStorageClient GetBlob() Response: "
                     << context.response;

          auto result_value = context.response->blob().data();
          if (cb_result_value_ != result_value) {
            cb_result_value_ = result_value;

            absl::Status roma_result = dispatcher_.LoadSync("v1", result_value);
            PS_VLOG(1) << "Roma Client Response: " << roma_result;
            if (roma_result.ok()) {
              PS_VLOG(2) << "Current code loaded into Roma:\n" << result_value;
            }
          }
        } else {
          PS_VLOG(0) << "Failed to Blob Fetch: "
                     << GetErrorMessage(result.status_code);
        }

        // Schedules the next code blob fetch and saves that task into task_id_.
        task_id_ = executor_->RunAfter(fetch_period_ms_,
                                       [this]() { PeriodicBucketFetch(); });
      });

  auto get_blob_result = blob_storage_client_->GetBlob(get_blob_context);
  if (!get_blob_result.Successful()) {
    PS_VLOG(0) << "BlobStorageClient -> GetBlob() failed: "
               << GetErrorMessage(get_blob_result.status_code);
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
