/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_CODE_FETCHER_H_
#define SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_CODE_FETCHER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/clients/code_dispatcher/udf_code_loader_interface.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "services/common/data_fetch/periodic_bucket_fetcher.h"
#include "services/common/data_fetch/version_util.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"
#include "src/concurrent/executor.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
class PeriodicBucketCodeFetcher : public PeriodicBucketFetcher {
 public:
  // Constructs a new PeriodicBucketCodeFetcher.
  explicit PeriodicBucketCodeFetcher(absl::string_view bucket_name,
                                     absl::Duration fetch_period_ms,
                                     UdfCodeLoaderInterface* loader,
                                     server_common::Executor* executor,
                                     WrapCodeForDispatch wrap_code,
                                     BlobStorageClient* blob_storage_client);

  ~PeriodicBucketCodeFetcher() { End(); }

  // Not copyable or movable.
  PeriodicBucketCodeFetcher(const PeriodicBucketCodeFetcher&) = delete;
  PeriodicBucketCodeFetcher& operator=(const PeriodicBucketCodeFetcher&) =
      delete;

 protected:
  // Handles the fetch context (containing the version as well as the data
  // fetched) by loading it into Roma.
  absl::Status OnFetch(
      const privacysandbox::apis::parc::v0::GetBlobRequest blob_request,
      const std::string blob_data) override;

  // Handles the fetch context (containing the version as well as the data
  // fetched) by loading it into Roma.
  absl::Status OnFetch(
      const google::scp::core::AsyncContext<
          google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
          google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
          context) override;

 private:
  WrapCodeForDispatch wrap_code_;
  UdfCodeLoaderInterface& loader_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_CODE_FETCHER_H_
