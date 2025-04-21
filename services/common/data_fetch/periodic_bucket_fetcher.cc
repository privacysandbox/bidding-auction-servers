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

#include "services/common/data_fetch/periodic_bucket_fetcher.h"

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
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "services/common/data_fetch/periodic_bucket_fetcher_metrics.h"
#include "services/common/data_fetch/version_util.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"
#include "src/core/interface/async_context.h"
#include "src/core/interface/errors.h"
#include "src/public/core/interface/execution_result.h"
#include "src/public/cpio/proto/blob_storage_service/v1/blob_storage_service.pb.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::Status PeriodicBucketFetcher::Start() {
  PeriodicBucketFetchSync();
  absl::MutexLock lock(&some_load_success_mu_);
  if (some_load_success_) {
    return absl::OkStatus();
  } else {
    return absl::InternalError("No code blob loaded successfully.");
  }
}

void PeriodicBucketFetcher::End() {
  if (task_id_) {
    executor_.Cancel(*task_id_);
    task_id_ = absl::nullopt;
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
