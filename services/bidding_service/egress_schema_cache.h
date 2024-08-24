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

#ifndef SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_CACHE_H_
#define SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_CACHE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "services/bidding_service/cddl_spec_cache.h"
#include "services/bidding_service/egress_features/egress_feature.h"

namespace privacy_sandbox::bidding_auction_servers {

// Cache to store the validation status of adtech provided schema.
//
// Usage:
// 1. This cache will be updated by the egress schema fetcher when it fetches
//    schemas from adtech provided endpoints.
// 2. This cache will be queried by the bidding service when it has to validate
//    the adtech generated egress payload in generateBid.
//
// This class is thread-safe.
class EgressSchemaCache {
 public:
  explicit EgressSchemaCache(
      std::unique_ptr<const CddlSpecCache> cddl_spec_cache);
  virtual ~EgressSchemaCache() = default;

  // Updates the cache with the provided schema.
  virtual absl::Status Update(absl::string_view egress_schema)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Gets the list of features corresponding to the egress schema matching the
  // specified egress schema version (ranging from 0-7, inclusive). Method
  // throws a non-ok Status if the schema version passed in was not loaded
  // previously OR if it was malformed OR didn't conform to its CDDL spec.
  virtual absl::StatusOr<std::vector<std::unique_ptr<EgressFeature>>> Get(
      int schema_version) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  absl::Mutex mu_;

  // Mapping from adtech egress schema version to list of feature objects in
  // that schema.
  absl::flat_hash_map<int, std::vector<std::unique_ptr<EgressFeature>>>
      version_features_ ABSL_GUARDED_BY(mu_);

  // Read only cache to find the CDDL spec against which the adtech provided
  // schema is to be validated against.
  std::unique_ptr<const CddlSpecCache> cddl_spec_cache_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_CACHE_H_
