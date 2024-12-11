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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_DOUBLY_LINKED_LIST_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_DOUBLY_LINKED_LIST_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "services/common/util/event.h"

namespace privacy_sandbox::bidding_auction_servers {

// K-anon data stored in the DLL.
struct KAnonHashData {
  std::string hash;
  std::unique_ptr<Event> timer_event = nullptr;
};

// DLL Node.
struct Node {
  std::unique_ptr<KAnonHashData> data;
  Node* next = nullptr;
  Node* prev = nullptr;

  explicit Node(std::unique_ptr<KAnonHashData> k_anon_hash_data = nullptr)
      : data(std::move(k_anon_hash_data)) {}
};

// Doubly linked list to back up the k-anon cache.
// This helps the cache keep the data sorted in LRU order (nodes
// towards the tail are LRU and nodes towards the head are MRU)
class DoublyLinkedList {
 public:
  DoublyLinkedList();
  ~DoublyLinkedList();
  Node* InsertAtFront(std::unique_ptr<KAnonHashData> k_anon_hash_data);
  void MoveToFront(Node* node);
  void Remove(Node* node);
  Node* Tail();

 private:
  std::unique_ptr<Node> head_;
  std::unique_ptr<Node> tail_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_DOUBLY_LINKED_LIST_H_
