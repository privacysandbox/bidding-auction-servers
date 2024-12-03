/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/seller_frontend_service/k_anon/doubly_linked_list.h"

#include "absl/strings/string_view.h"
#include "include/gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestHash1[] = "hash1";
constexpr char kTestHash2[] = "hash2";

class DoublyLinkedListTest : public ::testing::Test {
 protected:
  DoublyLinkedList dll_;

  std::unique_ptr<KAnonHashData> TestHashData1() {
    return std::make_unique<KAnonHashData>(KAnonHashData{
        .hash = kTestHash1,
    });
  }

  std::unique_ptr<KAnonHashData> TestHashData2() {
    return std::make_unique<KAnonHashData>(KAnonHashData{
        .hash = kTestHash2,
    });
  }
};

TEST_F(DoublyLinkedListTest, EmptyList) { EXPECT_EQ(dll_.Tail(), nullptr); }

TEST_F(DoublyLinkedListTest, CanAddNode) {
  auto* node = dll_.InsertAtFront(TestHashData1());
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->data->hash, kTestHash1);

  ASSERT_NE(dll_.Tail(), nullptr);
  EXPECT_EQ(dll_.Tail()->data->hash, kTestHash1);
}

TEST_F(DoublyLinkedListTest, CanRemoveNode) {
  ASSERT_EQ(dll_.Tail(), nullptr) << "Expected no item in an empty list";

  auto* node = dll_.InsertAtFront(TestHashData1());
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->data->hash, kTestHash1);

  ASSERT_NE(dll_.Tail(), nullptr);
  dll_.Remove(dll_.Tail());

  EXPECT_EQ(dll_.Tail(), nullptr) << "Expected no item in an empty list";
}

TEST_F(DoublyLinkedListTest, InsertsInOrder) {
  ASSERT_EQ(dll_.Tail(), nullptr) << "Expected no item in an empty list";

  dll_.InsertAtFront(TestHashData1());
  EXPECT_EQ(dll_.Tail()->data->hash, kTestHash1);

  dll_.InsertAtFront(TestHashData2());
  // Tail node still has hash1
  EXPECT_EQ(dll_.Tail()->data->hash, kTestHash1);

  // Remove the tail node and the new tail node should now contain hash2
  dll_.Remove(dll_.Tail());
  EXPECT_EQ(dll_.Tail()->data->hash, kTestHash2);

  // Remove tail node again and there should be no node remaining in the list.
  dll_.Remove(dll_.Tail());
  EXPECT_EQ(dll_.Tail(), nullptr);
}

TEST_F(DoublyLinkedListTest, MovesToFront) {
  ASSERT_EQ(dll_.Tail(), nullptr) << "Expected no item in an empty list";

  dll_.InsertAtFront(TestHashData1());
  EXPECT_EQ(dll_.Tail()->data->hash, kTestHash1);

  dll_.InsertAtFront(TestHashData2());
  // Tail node still has hash1
  EXPECT_EQ(dll_.Tail()->data->hash, kTestHash1);

  // Move the tail node to front.
  dll_.MoveToFront(dll_.Tail());
  EXPECT_EQ(dll_.Tail()->data->hash, kTestHash2);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
