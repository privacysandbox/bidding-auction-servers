// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/byob/buyer_code_fetch_manager_byob.h"

#include <string>
#include <utility>
#include <vector>

#include "services/bidding_service/bidding_code_fetch_config.pb.h"
#include "services/bidding_service/buyer_code_fetch_manager.h"
#include "services/bidding_service/constants.h"
#include "services/common/util/file_util.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::Status BuyerCodeFetchManagerByob::InitializeLocalCodeFetch() {
  if (udf_config_.bidding_executable_path().empty()) {
    return absl::UnavailableError(kLocalFetchNeedsPath);
  }
  PS_ASSIGN_OR_RETURN(auto adtech_code_blob,
                      GetFileContent(udf_config_.bidding_executable_path(),
                                     /*log_on_error=*/true));
  return loader_.LoadSync(kProtectedAudienceGenerateBidBlobVersion,
                          adtech_code_blob);
}

absl::Status BuyerCodeFetchManagerByob::InitializeBucketCodeFetch() {
  PS_RETURN_IF_ERROR(InitializeBucketClient());
  PS_RETURN_IF_ERROR(InitializeBucketCodeFetchForPA());
  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManagerByob::InitializeBucketCodeFetchForPA() {
  auto wrap_code = [](const std::vector<std::string>& adtech_code_blobs) {
    if (adtech_code_blobs.empty()) {
      return std::string("");
    }
    return adtech_code_blobs[0];
  };

  PS_ASSIGN_OR_RETURN(
      pa_udf_fetcher_,
      StartBucketFetch(
          udf_config_.protected_auction_bidding_executable_bucket(),
          udf_config_
              .protected_auction_bidding_executable_bucket_default_blob(),
          kProtectedAuctionExecutableId,
          absl::Milliseconds(udf_config_.url_fetch_period_ms()),
          std::move(wrap_code)));

  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManagerByob::InitializeBucketCodeFetchForPAS() {
  return absl::InvalidArgumentError(kProtectedAppSignalsMustBeDisabled);
}

absl::Status BuyerCodeFetchManagerByob::InitializeUrlCodeFetch() {
  return InitializeUrlCodeFetchForPA();
  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManagerByob::InitializeUrlCodeFetchForPA() {
  auto wrap_code = [](const std::vector<std::string>& adtech_code_blobs) {
    if (adtech_code_blobs.empty()) {
      return std::string("");
    }
    return adtech_code_blobs[0];
  };

  PS_ASSIGN_OR_RETURN(
      pa_udf_fetcher_,
      StartUrlFetch(udf_config_.bidding_executable_url(),
                    kProtectedAudienceGenerateBidBlobVersion,
                    kProtectedAuctionExecutableUrlId,
                    absl::Milliseconds(udf_config_.url_fetch_period_ms()),
                    absl::Milliseconds(udf_config_.url_fetch_timeout_ms()),
                    std::move(wrap_code)));

  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManagerByob::InitializeUrlCodeFetchForPAS() {
  return absl::InvalidArgumentError(kProtectedAppSignalsMustBeDisabled);
}

}  // namespace privacy_sandbox::bidding_auction_servers
