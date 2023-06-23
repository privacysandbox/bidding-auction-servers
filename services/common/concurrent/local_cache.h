//  Copyright 2022 Google LLC
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

#ifndef FLEDGE_SERVICES_COMMON_CONCURRENT_LOCAL_CACHE_H_
#define FLEDGE_SERVICES_COMMON_CONCURRENT_LOCAL_CACHE_H_

namespace privacy_sandbox::bidding_auction_servers {

// The class implementing this interface provides a thread-safe cache
// of keys and values. It guarantees non-blocking read/write.
// Different classes can implement different read-write mechanisms, and
// different expire mechanisms.
template <class Key, class Value>
class LocalCache {
 public:
  // Polymorphic class => virtual destructor
  virtual ~LocalCache() = default;

  // Get the corresponding Value for a given Key from cache or data source
  virtual Value LookUp(Key key) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_COMMON_CONCURRENT_LOCAL_CACHE_H_
