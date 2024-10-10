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

#ifndef SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_BYOB_BYOB_DISPATCH_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_BYOB_BYOB_DISPATCH_CLIENT_H_

#include <memory>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "services/common/clients/code_dispatcher/udf_code_loader_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

// Classes implementing this template and interface are able to execute
// asynchronous requests using ROMA BYOB.
template <typename ServiceRequest, typename ServiceResponse>
class ByobDispatchClient : public UdfCodeLoaderInterface {
 public:
  // Polymorphic class => virtual destructor
  virtual ~ByobDispatchClient() = default;

  // Loads new execution code synchronously. The class implementing this method
  // must track the version currently loaded into ROMA BYOB. If the class
  // supports multiple versions, it needs to track and be able to provide a
  // means of executing different versions of the UDF code.
  //
  // version: the new version string of the code to load
  // code: the code string to load
  // return: a status indicating whether the code load was successful.
  virtual absl::Status LoadSync(std::string version, std::string code) = 0;

  // Executes a single request synchronously.
  //
  // request: the request object.
  // timeout: the maximum time this will block for.
  // return: the output of the execution.
  virtual absl::StatusOr<std::unique_ptr<ServiceResponse>> Execute(
      const ServiceRequest& request, absl::Duration timeout) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_BYOB_BYOB_DISPATCH_CLIENT_H_
