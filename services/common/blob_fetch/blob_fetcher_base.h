/*
 * Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_BASE_H_
#define SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_BASE_H_

#include <string>
#include <vector>

#include "absl/status/status.h"

namespace privacy_sandbox::bidding_auction_servers {

class BlobFetcherBase {
 public:
  // A non-owning view of Blob used to avoid copy large chunk of data stored in
  // Blob. Must not outlive the original Blob.
  struct BlobView {
    absl::string_view path;
    absl::string_view bytes;
  };

  // A pair of file path and byte string.
  struct Blob {
    std::string path;
    std::string bytes;

    Blob(absl::string_view path, absl::string_view bytes)
        : path(path), bytes(bytes) {}
    BlobView CreateBlobView() const {
      return BlobView{.path = path, .bytes = bytes};
    }
  };

  // Provides optional filters for blobs to fetch
  struct FilterOptions {
    std::vector<std::string> included_prefixes;
  };

  virtual ~BlobFetcherBase() = default;

  // Accesses the current snapshot of the blob.
  virtual const std::vector<Blob>& snapshot() const = 0;

  // Fetches the bucket synchronously.
  virtual absl::Status FetchSync(
      const FilterOptions& filter_option = FilterOptions()) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_BASE_H_
