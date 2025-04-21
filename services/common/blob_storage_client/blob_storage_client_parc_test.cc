/*
 * Copyright 2025 Google LLC
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

#include "services/common/blob_storage_client/blob_storage_client_parc.h"

#include <utility>

#include <grpcpp/grpcpp.h>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service_mock.grpc.pb.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_parc_utils.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::google::scp::core::test::EqualsProto;
using grpc::ClientContext;
using grpc::Status;
using ::privacysandbox::apis::parc::v0::BlobMetadata;
using ::privacysandbox::apis::parc::v0::GetBlobRequest;
using ::privacysandbox::apis::parc::v0::GetBlobResponse;
using ::privacysandbox::apis::parc::v0::ListBlobsMetadataRequest;
using ::privacysandbox::apis::parc::v0::ListBlobsMetadataResponse;
using ::privacysandbox::apis::parc::v0::MockParcServiceStub;
using ::testing::_;
using ::testing::Return;

template <class R>
class MockClientReader : public grpc::ClientReaderInterface<R> {
 public:
  MockClientReader() = default;

  /// ClientStreamingInterface
  MOCK_METHOD0_T(Finish, Status());

  /// ReaderInterface
  MOCK_METHOD1_T(NextMessageSize, bool(uint32_t*));
  MOCK_METHOD1_T(Read, bool(R*));

  /// ClientReaderInterface
  MOCK_METHOD0_T(WaitForInitialMetadata, void());
};

class BlobStorageClientParcTest : public testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(BlobStorageClientParcTest, GetBlobCallClientGetBlobStreamRPC) {
  GetBlobRequest get_blob_request;
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();
  auto* mock_reader = new MockClientReader<GetBlobResponse>();
  GetBlobResponse empty_response;

  EXPECT_CALL(*mock_reader, Read(_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(empty_response),
                                 Return(true)))
      .WillOnce(Return(false));

  EXPECT_CALL(*parc_client_stub, GetBlobRaw(_, _))
      .WillOnce(Return(mock_reader));
  EXPECT_CALL(*mock_reader, Finish()).WillOnce(Return(Status::OK));
  ParcBlobStorageClient parc_client =
      ParcBlobStorageClient(std::move(parc_client_stub));
  auto res = GetBlobFromResultParc(parc_client.GetBlob(get_blob_request));

  EXPECT_TRUE(res.ok());
  EXPECT_EQ(res.value(), "");
}

TEST_F(BlobStorageClientParcTest, GetBlobStreamMergedWhenReturn) {
  GetBlobRequest get_blob_request;
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();
  auto* mock_reader = new MockClientReader<GetBlobResponse>();

  GetBlobResponse response_one;
  response_one.set_data("One");
  GetBlobResponse response_two;
  response_two.set_data("Two");

  EXPECT_CALL(*mock_reader, Read(_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(response_one),
                                 Return(true)))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(response_two),
                                 Return(true)))
      .WillOnce(Return(false));

  EXPECT_CALL(*parc_client_stub, GetBlobRaw(_, _))
      .WillOnce(Return(mock_reader));

  EXPECT_CALL(*mock_reader, Finish()).WillOnce(Return(Status::OK));
  ParcBlobStorageClient parc_client =
      ParcBlobStorageClient(std::move(parc_client_stub));
  auto res = GetBlobFromResultParc(parc_client.GetBlob(get_blob_request));

  EXPECT_TRUE(res.ok());
  EXPECT_EQ(res.value(), "OneTwo");
}

TEST_F(BlobStorageClientParcTest,
       ListBlobsMetadataCallClientListBlobsMetadataRPC) {
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();
  ListBlobsMetadataRequest list_blob_metadata_request;
  ListBlobsMetadataResponse expected_response;
  EXPECT_CALL(*parc_client_stub,
              ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request), _))
      .WillOnce(Return(grpc::Status::OK));
  ParcBlobStorageClient parc_client =
      ParcBlobStorageClient(std::move(parc_client_stub));
  auto res = ListBlobsMetadataFromResultParc(
      parc_client.ListBlobsMetadata(list_blob_metadata_request));

  EXPECT_TRUE(res.ok());
  EXPECT_THAT(res.value(), EqualsProto(expected_response));
}

TEST_F(BlobStorageClientParcTest, ListBlobsMetadataFetchAllResultAndMerged) {
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();
  ListBlobsMetadataRequest list_blob_metadata_request_one;
  BlobMetadata blob_metadata_one;
  blob_metadata_one.set_bucket_name("bucket_1");
  blob_metadata_one.set_blob_name("blob_1");
  ListBlobsMetadataResponse list_blob_metadata_response_one;
  *list_blob_metadata_response_one.add_blob_metadatas() = blob_metadata_one;
  list_blob_metadata_response_one.set_next_page_token("Next1");

  ListBlobsMetadataRequest list_blob_metadata_request_two;
  list_blob_metadata_request_two.set_page_token("Next1");
  BlobMetadata blob_metadata_two;
  blob_metadata_two.set_bucket_name("bucket_2");
  blob_metadata_two.set_blob_name("blob_2");
  ListBlobsMetadataResponse list_blob_metadata_response_two;
  *list_blob_metadata_response_two.add_blob_metadatas() = blob_metadata_two;

  EXPECT_CALL(
      *parc_client_stub,
      ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request_one), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response_one),
          Return(grpc::Status::OK)));

  EXPECT_CALL(
      *parc_client_stub,
      ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request_two), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response_two),
          Return(grpc::Status::OK)));

  ParcBlobStorageClient parc_client =
      ParcBlobStorageClient(std::move(parc_client_stub));
  auto res = ListBlobsMetadataFromResultParc(
      parc_client.ListBlobsMetadata(list_blob_metadata_request_one));

  ListBlobsMetadataResponse expected_response;
  *expected_response.add_blob_metadatas() = blob_metadata_one;
  *expected_response.add_blob_metadatas() = blob_metadata_two;

  EXPECT_TRUE(res.ok());
  EXPECT_THAT(res.value(), EqualsProto(expected_response));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
