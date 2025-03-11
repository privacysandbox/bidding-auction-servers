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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_CACHE_DOUBLY_LINKED_LIST_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_CACHE_DOUBLY_LINKED_LIST_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "services/common/util/event.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

// K-anon data stored in the DLL.
template <typename KeyT, typename ValueT>
struct CacheHashData {
  KeyT key;
  ValueT value;
  std::unique_ptr<Event> timer_event = nullptr;
};

// DLL Node.
template <typename KeyT, typename ValueT>
struct Node {
  std::unique_ptr<CacheHashData<KeyT, ValueT>> data;
  Node* next = nullptr;
  Node* prev = nullptr;

  explicit Node(
      std::unique_ptr<CacheHashData<KeyT, ValueT>> cache_hash_data = nullptr)
      : data(std::move(cache_hash_data)) {}
};

// Doubly linked list to back up the k-anon cache.
// This helps the cache keep the data sorted in LRU order (nodes
// towards the tail are LRU and nodes towards the head are MRU)
template <typename KeyT, typename ValueT>
class DoublyLinkedList {
 public:
  DoublyLinkedList()
      : head_(std::make_unique<Node<KeyT, ValueT>>()),
        tail_(std::make_unique<Node<KeyT, ValueT>>()) {
    head_->next = tail_.get();
    tail_->prev = head_.get();
  }

  ~DoublyLinkedList() {
    auto* cur = head_->next;
    while (cur != tail_.get()) {
      auto* tmp_next = cur->next;
      cur->next = nullptr;
      cur->prev = nullptr;
      delete cur;
      cur = tmp_next;
    }
    head_->next = nullptr;
    tail_->prev = nullptr;
  }

  Node<KeyT, ValueT>* InsertAtFront(
      std::unique_ptr<CacheHashData<KeyT, ValueT>> cache_hash_data) {
    auto node =
        std::make_unique<Node<KeyT, ValueT>>(std::move(cache_hash_data));
    auto* node_ptr = node.release();
    node_ptr->next = head_->next;
    node_ptr->prev = head_.get();
    // Head's next is guaranteed to be present since we use a sentinel for tail
    // as well.
    head_->next->prev = node_ptr;
    head_->next = node_ptr;
    return node_ptr;
  }

  void MoveToFront(Node<KeyT, ValueT>* node) {
    // Disconnect from neighbors
    node->prev->next = node->next;
    node->next->prev = node->prev;

    // Link at front
    node->next = head_->next;
    node->prev = head_.get();
    head_->next->prev = node;
    head_->next = node;
  }

  void Remove(Node<KeyT, ValueT>* node) {
    PS_VLOG(5) << __func__ << ": Removing node";
    auto node_to_clean = std::unique_ptr<Node<KeyT, ValueT>>(node);
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node_to_clean.reset();
  }

  Node<KeyT, ValueT>* Tail() {
    // Head and tail are always sentinels, so if they point at each other that
    // implies an empty list.
    if (tail_->prev == head_.get()) {
      return nullptr;
    }

    return tail_->prev;
  }

 private:
  std::unique_ptr<Node<KeyT, ValueT>> head_;
  std::unique_ptr<Node<KeyT, ValueT>> tail_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_CACHE_DOUBLY_LINKED_LIST_H_
