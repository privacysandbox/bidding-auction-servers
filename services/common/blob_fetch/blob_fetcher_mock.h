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

#ifndef SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_MOCK_H_
#define SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_MOCK_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "services/common/blob_fetch/blob_fetcher_base.h"

namespace privacy_sandbox::bidding_auction_servers {

class BlobFetcherMock : public BlobFetcherBase {
 public:
  MOCK_METHOD(const std::vector<Blob>&, snapshot, (), (const, override));
  MOCK_METHOD(absl::Status, FetchSync, (const FilterOptions& filter_options),
              (override));
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_MOCK_H_
