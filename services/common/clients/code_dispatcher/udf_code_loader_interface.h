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

#ifndef SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_UDF_CODE_LOADER_INTERFACE_H_
#define SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_UDF_CODE_LOADER_INTERFACE_H_

#include "absl/status/status.h"

namespace privacy_sandbox::bidding_auction_servers {

// Classes implementing this interface provide a method to load AdTech UDF
// (eg. generateBid, scoreAd, etc.) code into an execution engine. Implementing
// classes should also provide a means of execution of this loaded UDF code in
// the execution engine.
class UdfCodeLoaderInterface {
 public:
  // Polymorphic class => virtual destructor
  virtual ~UdfCodeLoaderInterface() = default;

  // Loads new execution code synchronously. The class implementing this method
  // must track the version currently loaded into the underlying execution
  // engine. If the class supports multiple versions, it needs to track and be
  // able to provide a means of executing different versions of the UDF code.
  //
  // version: the new version string of the code to load
  // code: the code string to load
  // return: a status indicating whether the code load was successful.
  // NOLINTNEXTLINE
  virtual absl::Status LoadSync(std::string version, std::string code) {
    return absl::NotFoundError("Method not implemented.");
  }
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_UDF_CODE_LOADER_INTERFACE_H_
