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

#ifndef FLEDGE_SERVICES_COMMON_CLIENTS_CLIENT_FACTORY_H_
#define FLEDGE_SERVICES_COMMON_CLIENTS_CLIENT_FACTORY_H_

#include <memory>
#include <utility>
#include <vector>

namespace privacy_sandbox::bidding_auction_servers {

// The class implementing this interface provides different Client Class objects
// based on some differentiating factor. Eg. A gRPC client factory could be
// differentiating clients based on different channels/host addresses.
template <class Client, class ClientKey>
class ClientFactory {
 public:
  // Polymorphic class => virtual destructor
  virtual ~ClientFactory() = default;

  // This function is used to get an instance of a client object
  // client_key: This is a value for the key based on which the different
  // client objects are differentiated.
  virtual std::shared_ptr<Client> Get(ClientKey client_key) const = 0;

  // Returns a <Key, Value> pair of all entries contained by the factory.
  virtual std::vector<std::pair<ClientKey, std::shared_ptr<Client>>> Entries()
      const = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_COMMON_CLIENTS_CLIENT_FACTORY_H_
