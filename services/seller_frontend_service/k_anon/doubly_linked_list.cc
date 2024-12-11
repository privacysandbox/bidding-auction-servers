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

#include "services/seller_frontend_service/k_anon/doubly_linked_list.h"

#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

DoublyLinkedList::DoublyLinkedList()
    : head_(std::make_unique<Node>()), tail_(std::make_unique<Node>()) {
  head_->next = tail_.get();
  tail_->prev = head_.get();
}

DoublyLinkedList::~DoublyLinkedList() {
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

Node* DoublyLinkedList::InsertAtFront(
    std::unique_ptr<KAnonHashData> k_anon_hash_data) {
  auto node = std::make_unique<Node>(std::move(k_anon_hash_data));
  auto* node_ptr = node.release();
  node_ptr->next = head_->next;
  node_ptr->prev = head_.get();
  // Head's next is guaranteed to be present since we use a sentinel for tail
  // as well.
  head_->next->prev = node_ptr;
  head_->next = node_ptr;
  return node_ptr;
}

void DoublyLinkedList::MoveToFront(Node* node) {
  // Disconnect from neighbors
  node->prev->next = node->next;
  node->next->prev = node->prev;

  // Link at front
  node->next = head_->next;
  node->prev = head_.get();
  head_->next->prev = node;
  head_->next = node;
}

void DoublyLinkedList::Remove(Node* node) {
  PS_VLOG(5) << __func__ << ": Removing node";
  auto node_to_clean = std::unique_ptr<Node>(node);
  node->prev->next = node->next;
  node->next->prev = node->prev;
  node_to_clean.reset();
}

Node* DoublyLinkedList::Tail() {
  // Head and tail are always sentinels, so if they point at each other that
  // implies an empty list.
  if (tail_->prev == head_.get()) {
    return nullptr;
  }

  return tail_->prev;
}

}  // namespace privacy_sandbox::bidding_auction_servers
