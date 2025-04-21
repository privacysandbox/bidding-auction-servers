// Copyright 2025 Google LLC
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

#include <memory>
#include <utility>

#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_cpio.h"
#include "services/common/util/blob_storage_client_utils.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

std::unique_ptr<BlobStorageClient> BuildBlobStorageClient() {
  std::unique_ptr<::google::scp::cpio::BlobStorageClientInterface>
      blob_storage_client =
          ::google::scp::cpio::BlobStorageClientFactory::Create();
  return std::make_unique<CpioBlobStorageClient>(
      std::move(blob_storage_client));
}
}  // namespace privacy_sandbox::bidding_auction_servers
