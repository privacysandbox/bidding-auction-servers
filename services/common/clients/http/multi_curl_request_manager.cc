// Copyright 2022 Google LLC
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

#include "services/common/clients/http/multi_curl_request_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

MultiCurlRequestManager::MultiCurlRequestManager() {
  running_handles_ = 0;
  curl_global_init(CURL_GLOBAL_ALL);
  request_manager_ = curl_multi_init();
}

MultiCurlRequestManager::~MultiCurlRequestManager() {
  // Cancel all requests and exit.
  curl_multi_cleanup(request_manager_);
  curl_global_cleanup();
}

CURLMcode MultiCurlRequestManager::Add(CURL* curl_handle)
    ABSL_LOCKS_EXCLUDED(request_manager_mu_) {
  absl::MutexLock l(&request_manager_mu_);
  curl_multi_add_handle(request_manager_, curl_handle);
  CURLMcode mc = curl_multi_perform(request_manager_, &running_handles_);
  return mc;
}

CURLMsg* MultiCurlRequestManager::GetUpdate(int* msgs_left)
    ABSL_LOCKS_EXCLUDED(request_manager_mu_) {
  absl::MutexLock l(&request_manager_mu_);
  curl_multi_perform(request_manager_, &running_handles_);
  CURLMsg* msg = curl_multi_info_read(request_manager_, msgs_left);
  return msg;
}
CURLMcode MultiCurlRequestManager::Remove(CURL* curl_handle)
    ABSL_LOCKS_EXCLUDED(request_manager_mu_) {
  absl::MutexLock l(&request_manager_mu_);
  return curl_multi_remove_handle(request_manager_, curl_handle);
}

}  // namespace privacy_sandbox::bidding_auction_servers
