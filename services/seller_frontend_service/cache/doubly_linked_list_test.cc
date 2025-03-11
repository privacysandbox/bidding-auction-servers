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

#include "services/seller_frontend_service/cache/doubly_linked_list.h"

#include "absl/strings/string_view.h"
#include "include/gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestKey1[] = "key1";
constexpr char kTestKey2[] = "key2";

class DoublyLinkedListTest : public ::testing::Test {
 protected:
  DoublyLinkedList<std::string, std::string> dll_;

  std::unique_ptr<CacheHashData<std::string, std::string>> TestHashData1() {
    return std::make_unique<CacheHashData<std::string, std::string>>(
        CacheHashData<std::string, std::string>{
            .key = kTestKey1,
        });
  }

  std::unique_ptr<CacheHashData<std::string, std::string>> TestHashData2() {
    return std::make_unique<CacheHashData<std::string, std::string>>(
        CacheHashData<std::string, std::string>{
            .key = kTestKey2,
        });
  }
};

TEST_F(DoublyLinkedListTest, EmptyList) { EXPECT_EQ(dll_.Tail(), nullptr); }

TEST_F(DoublyLinkedListTest, CanAddNode) {
  auto* node = dll_.InsertAtFront(TestHashData1());
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->data->key, kTestKey1);

  ASSERT_NE(dll_.Tail(), nullptr);
  EXPECT_EQ(dll_.Tail()->data->key, kTestKey1);
}

TEST_F(DoublyLinkedListTest, CanRemoveNode) {
  ASSERT_EQ(dll_.Tail(), nullptr) << "Expected no item in an empty list";

  auto* node = dll_.InsertAtFront(TestHashData1());
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->data->key, kTestKey1);

  ASSERT_NE(dll_.Tail(), nullptr);
  dll_.Remove(dll_.Tail());

  EXPECT_EQ(dll_.Tail(), nullptr) << "Expected no item in an empty list";
}

TEST_F(DoublyLinkedListTest, InsertsInOrder) {
  ASSERT_EQ(dll_.Tail(), nullptr) << "Expected no item in an empty list";

  dll_.InsertAtFront(TestHashData1());
  EXPECT_EQ(dll_.Tail()->data->key, kTestKey1);

  dll_.InsertAtFront(TestHashData2());
  // Tail node still has key1.
  EXPECT_EQ(dll_.Tail()->data->key, kTestKey1);

  // Remove the tail node and the new tail node should now contain key2.
  dll_.Remove(dll_.Tail());
  EXPECT_EQ(dll_.Tail()->data->key, kTestKey2);

  // Remove tail node again and there should be no node remaining in the list.
  dll_.Remove(dll_.Tail());
  EXPECT_EQ(dll_.Tail(), nullptr);
}

TEST_F(DoublyLinkedListTest, MovesToFront) {
  ASSERT_EQ(dll_.Tail(), nullptr) << "Expected no item in an empty list";

  dll_.InsertAtFront(TestHashData1());
  EXPECT_EQ(dll_.Tail()->data->key, kTestKey1);

  dll_.InsertAtFront(TestHashData2());
  // Tail node still has key1.
  EXPECT_EQ(dll_.Tail()->data->key, kTestKey1);

  // Move the tail node to front.
  dll_.MoveToFront(dll_.Tail());
  EXPECT_EQ(dll_.Tail()->data->key, kTestKey2);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
