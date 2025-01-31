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

#ifndef SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_FETCHER_METRICS_H_
#define SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_FETCHER_METRICS_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "services/auction_service/udf_fetcher/auction_code_fetch_config.pb.h"
#include "services/bidding_service/bidding_code_fetch_config.pb.h"
#include "services/bidding_service/egress_schema_fetch_config.pb.h"
#include "services/common/data_fetch/version_util.h"
#include "services/common/metric/server_definition.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

// Class that records metrics associated with the bucket blob fetcher.
// These metrics are not in the critical request path.
class PeriodicBucketFetcherMetrics {
 public:
  static absl::Status RegisterBiddingServiceMetrics(
      bool enable_protected_app_signals, bool enable_protected_audience,
      const bidding_service::EgressSchemaFetchConfig&
          egress_schema_fetch_config,
      const bidding_service::BuyerCodeFetchConfig& udf_config) {
    bool fetch_udfs_from_bucket =
        udf_config.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET;
    bool fetch_schema_from_bucket = egress_schema_fetch_config.fetch_mode() ==
                                    blob_fetch::FETCH_MODE_BUCKET;
    bool register_protected_app_signals =
        enable_protected_app_signals &&
        (fetch_udfs_from_bucket || fetch_schema_from_bucket);
    bool register_protected_audience =
        enable_protected_audience && fetch_udfs_from_bucket;
    if (!register_protected_app_signals && !register_protected_audience) {
      return absl::OkStatus();
    }
    auto* context_map = metric::BiddingContextMap();
    PS_RETURN_IF_ERROR(context_map->AddObserverable(
        metric::kBlobLoadStatus,
        PeriodicBucketFetcherMetrics::GetBlobLoadStatus));
    PS_RETURN_IF_ERROR(context_map->AddObserverable(
        metric::kAvailableBlobs,
        PeriodicBucketFetcherMetrics::GetAvailableBlobs));
    return absl::OkStatus();
  }

  static absl::Status RegisterAuctionServiceMetrics(
      const auction_service::SellerCodeFetchConfig& udf_config) {
    if (udf_config.fetch_mode() != blob_fetch::FETCH_MODE_BUCKET) {
      return absl::OkStatus();
    }
    auto* context_map = metric::AuctionContextMap();
    PS_RETURN_IF_ERROR(context_map->AddObserverable(
        metric::kBlobLoadStatus,
        PeriodicBucketFetcherMetrics::GetBlobLoadStatus));
    PS_RETURN_IF_ERROR(context_map->AddObserverable(
        metric::kAvailableBlobs,
        PeriodicBucketFetcherMetrics::GetAvailableBlobs));
    return absl::OkStatus();
  }

  static void UpdateBlobLoadMetrics(absl::string_view blob_name,
                                    absl::string_view bucket_name,
                                    const absl::Status& load_status) {
    const absl::StatusOr<std::string> full_blob_name =
        GetBucketBlobVersion(bucket_name, blob_name);
    const std::string reported_blob_name =
        full_blob_name.ok() ? *full_blob_name : std::string(blob_name);

    if (load_status.ok()) {
      PeriodicBucketFetcherMetrics::AddAvailableBlob(reported_blob_name);
    }
    PeriodicBucketFetcherMetrics::UpdateBlobLoadStatus(reported_blob_name,
                                                       load_status);
  }

  static absl::flat_hash_map<std::string, double> GetAvailableBlobs()
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    // TODO: after implementing blob eviction, we need to at least report once
    // that a blob is not available after eviction before removing its entry
    // from the map. Otherwise cloud monitoring systems could keep reporting the
    // last received value for a given blob.
    return available_blobs_;
  }

  // Useful for investigating why a blob is not appearing in GetAvailableBlobs.
  // If the server cannot list the bucket objects, no status will be populated.
  static absl::flat_hash_map<std::string, double> GetBlobLoadStatus()
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    return blob_load_status_;
  }

  static void AddAvailableBlob(const std::string& blob)
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    available_blobs_[blob] = 1;
  }

  static void UpdateBlobLoadStatus(const std::string& blob,
                                   const absl::Status& status)
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    blob_load_status_[blob] = static_cast<double>(status.code());
  }

  // Clears all metric counters.
  static void ClearStates_TestOnly() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    blob_load_status_.clear();
    available_blobs_.clear();
  }

 private:
  ABSL_CONST_INIT static inline absl::Mutex mu_{absl::kConstInit};

  static inline absl::flat_hash_map<std::string, double> blob_load_status_
      ABSL_GUARDED_BY(mu_){};

  static inline absl::flat_hash_map<std::string, double> available_blobs_
      ABSL_GUARDED_BY(mu_){};
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_FETCHER_METRICS_H_
