// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SERVICES_BIDDING_SERVICE_BYOB_GENERATE_BID_BYOB_DISPATCH_CLIENT_H_
#define SERVICES_BIDDING_SERVICE_BYOB_GENERATE_BID_BYOB_DISPATCH_CLIENT_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/synchronization/mutex.h"
#include "api/udf/generate_bid_roma_byob_app_service.h"
#include "api/udf/generate_bid_udf_interface.pb.h"
#include "services/common/clients/code_dispatcher/byob/byob_dispatch_client.h"
#include "src/roma/byob/config/config.h"
#include "src/roma/byob/utility/udf_blob.h"

namespace privacy_sandbox::bidding_auction_servers {

class GenerateBidByobDispatchClient
    : public ByobDispatchClient<
          roma_service::GenerateProtectedAudienceBidRequest,
          roma_service::GenerateProtectedAudienceBidResponse> {
 public:
  // Required for moving an instance (for eg. inside a factory function).
  GenerateBidByobDispatchClient(GenerateBidByobDispatchClient&& other) noexcept
      : byob_service_(std::move(other.byob_service_)) {}

  ~GenerateBidByobDispatchClient() override = default;

  // Factory method that creates a GenerateBidByobDispatchClient instance.
  //
  // num_workers: the number of workers to spin up in the execution environment
  // return: created instance if successful, a status indicating reason for
  // failure otherwise
  static absl::StatusOr<GenerateBidByobDispatchClient> Create(int num_workers);

  // Loads new execution code into ROMA BYOB synchronously. Tracks the code
  // token, version, and hash of the loaded code, if successful.
  //
  // version: the new version string of the code to load
  // code: the code string to load
  // return: a status indicating whether the code load was successful.
  absl::Status LoadSync(std::string version, std::string code) override;

  // Executes a single request synchronously.
  //
  // request: the request object.
  // timeout: the maximum time this will block for.
  // return: the output of the execution.
  absl::StatusOr<
      std::unique_ptr<roma_service::GenerateProtectedAudienceBidResponse>>
  Execute(const roma_service::GenerateProtectedAudienceBidRequest& request,
          absl::Duration timeout) override;

 private:
  // ROMA BYOB service that encapsulates the AdTech UDF interface.
  roma_service::ByobGenerateProtectedAudienceBidService<> byob_service_;

  // Unique ID used to identify the most recently loaded code blob.
  std::string code_token_;

  // AdTech code version corresponding to the most recently loaded code blob.
  std::string code_version_;

  // Hash of the most recently loaded code blob. Used to prevent loading the
  // same code multiple times.
  std::size_t code_hash_;

  // Mutex to protect information about the most recently loaded code blob.
  absl::Mutex code_mutex_;

  explicit GenerateBidByobDispatchClient(
      roma_service::ByobGenerateProtectedAudienceBidService<> byob_service)
      : byob_service_(std::move(byob_service)) {}
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BYOB_GENERATE_BID_BYOB_DISPATCH_CLIENT_H_
