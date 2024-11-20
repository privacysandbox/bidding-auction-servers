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

#ifndef SERVICES_BIDDING_SERVICE_CDDL_SPEC_CACHE_H_
#define SERVICES_BIDDING_SERVICE_CDDL_SPEC_CACHE_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Cache to load the available CDDL specs upon service start.
class CddlSpecCache {
 public:
  explicit CddlSpecCache(absl::string_view dir_path);
  virtual ~CddlSpecCache() = default;

  // Returns the contents of the spec matching the provided version number or
  // returns an error if there is no such version in the cache.
  virtual absl::StatusOr<absl::string_view> Get(
      absl::string_view version) const;

  // Initializes the cache with the available schema contents.
  virtual absl::Status Init();

 private:
  // Mapping from CDDL spec version => CDDL spec file contents.
  absl::flat_hash_map<std::string, std::string> version_contents_;
  // Directory under which the CDDL specs reside.
  std::string cddl_spec_dir_path_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_CDDL_SPEC_CACHE_H_
