// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <memory>
#include <utility>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service_mock.grpc.pb.h>

#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/common/blob_fetch/blob_fetcher.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_parc.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/core/interface/async_context.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::scp::core::test::EqualsProto;
using grpc::Status;
using ::testing::_;
using ::testing::Eq;
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

constexpr char kSampleBucketName[] = "BucketName";
constexpr char kSampleBlobName[] = "blob_name";
constexpr char kSampleData[] = "test";
constexpr char kExcludedBlobName[] = "excluded/blob";
constexpr char kIncludedBlobPrefix[] = "included";
constexpr char kIncludedBlobName[] = "included/blob";

class BlobFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }

  std::unique_ptr<MockExecutor> executor_ = std::make_unique<MockExecutor>();
};

TEST_F(BlobFetcherTest, FetchBucketParc) {
  EXPECT_CALL(*executor_, Run).WillOnce([](absl::AnyInvocable<void()> closure) {
    closure();
  });
  std::shared_ptr<privacysandbox::apis::parc::v0::MockParcServiceStub>
      parc_client_stub = std::make_unique<
          privacysandbox::apis::parc::v0::MockParcServiceStub>();

  // Set listblobmetadata mock.
  privacysandbox::apis::parc::v0::BlobMetadata md1;
  md1.set_bucket_name(kSampleBucketName);
  md1.set_blob_name(kSampleBlobName);

  privacysandbox::apis::parc::v0::ListBlobsMetadataRequest
      list_blob_metadata_request;
  privacysandbox::apis::parc::v0::ListBlobsMetadataResponse
      list_blob_metadata_response;
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md1));

  EXPECT_CALL(*parc_client_stub,
              ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response),
          Return(grpc::Status::OK)));

  auto* mock_reader_one =
      new MockClientReader<privacysandbox::apis::parc::v0::GetBlobResponse>();
  privacysandbox::apis::parc::v0::GetBlobRequest get_blob_request_one;
  privacysandbox::apis::parc::v0::GetBlobResponse get_blob_response_one;
  get_blob_request_one.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  get_blob_request_one.mutable_blob_metadata()->set_blob_name(kSampleBlobName);
  get_blob_response_one.set_data(kSampleData);

  EXPECT_CALL(*mock_reader_one, Read(_))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<0>(get_blob_response_one), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_one)))
      .WillOnce(Return(mock_reader_one));
  EXPECT_CALL(*mock_reader_one, Finish()).WillOnce(Return(Status::OK));

  std::unique_ptr<BlobStorageClient> parc_client =
      std::make_unique<ParcBlobStorageClient>(
          ParcBlobStorageClient(std::move(parc_client_stub)));

  BlobFetcher bucket_fetcher(kSampleBucketName, executor_.get(),
                             std::move(parc_client));
  EXPECT_TRUE(bucket_fetcher.FetchSync().ok());
}

TEST_F(BlobFetcherTest, OnlyFetchBlobWithGivenPrefixParc) {
  EXPECT_CALL(*executor_, Run).WillOnce([](absl::AnyInvocable<void()> closure) {
    closure();
  });
  std::shared_ptr<privacysandbox::apis::parc::v0::MockParcServiceStub>
      parc_client_stub = std::make_unique<
          privacysandbox::apis::parc::v0::MockParcServiceStub>();

  // Set listblobmetadata mock.
  privacysandbox::apis::parc::v0::BlobMetadata excluded_blob_metadata;
  excluded_blob_metadata.set_bucket_name(kSampleBucketName);
  excluded_blob_metadata.set_blob_name(kExcludedBlobName);
  privacysandbox::apis::parc::v0::BlobMetadata included_blob_metadata;
  included_blob_metadata.set_bucket_name(kSampleBucketName);
  included_blob_metadata.set_blob_name(kIncludedBlobName);

  privacysandbox::apis::parc::v0::ListBlobsMetadataRequest
      list_blob_metadata_request;
  privacysandbox::apis::parc::v0::ListBlobsMetadataResponse
      list_blob_metadata_response;
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_response.mutable_blob_metadatas()->Add(
      std::move(excluded_blob_metadata));
  list_blob_metadata_response.mutable_blob_metadatas()->Add(
      std::move(included_blob_metadata));

  EXPECT_CALL(*parc_client_stub,
              ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response),
          Return(grpc::Status::OK)));

  auto* mock_reader_one =
      new MockClientReader<privacysandbox::apis::parc::v0::GetBlobResponse>();
  privacysandbox::apis::parc::v0::GetBlobRequest get_blob_request_one;
  privacysandbox::apis::parc::v0::GetBlobResponse get_blob_response_one;
  get_blob_request_one.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  get_blob_request_one.mutable_blob_metadata()->set_blob_name(
      kIncludedBlobName);
  get_blob_response_one.set_data(kSampleData);

  EXPECT_CALL(*mock_reader_one, Read(_))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<0>(get_blob_response_one), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_one)))
      .WillOnce(Return(mock_reader_one));
  EXPECT_CALL(*mock_reader_one, Finish()).WillOnce(Return(Status::OK));

  std::unique_ptr<BlobStorageClient> parc_client =
      std::make_unique<ParcBlobStorageClient>(
          ParcBlobStorageClient(std::move(parc_client_stub)));

  BlobFetcher bucket_fetcher(kSampleBucketName, executor_.get(),
                             std::move(parc_client));
  EXPECT_TRUE(
      bucket_fetcher.FetchSync({.included_prefixes = {kIncludedBlobPrefix}})
          .ok());
}

TEST_F(BlobFetcherTest, FetchBucket_Failure_Parc) {
  EXPECT_CALL(*executor_, Run).WillOnce([](absl::AnyInvocable<void()> closure) {
    closure();
  });
  std::shared_ptr<privacysandbox::apis::parc::v0::MockParcServiceStub>
      parc_client_stub = std::make_unique<
          privacysandbox::apis::parc::v0::MockParcServiceStub>();

  // Set listblobmetadata mock.
  privacysandbox::apis::parc::v0::BlobMetadata md1;
  md1.set_bucket_name(kSampleBucketName);
  md1.set_blob_name(kSampleBlobName);

  privacysandbox::apis::parc::v0::ListBlobsMetadataRequest
      list_blob_metadata_request;
  privacysandbox::apis::parc::v0::ListBlobsMetadataResponse
      list_blob_metadata_response;
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md1));

  EXPECT_CALL(*parc_client_stub,
              ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response),
          Return(grpc::Status::OK)));

  auto* mock_reader_one =
      new MockClientReader<privacysandbox::apis::parc::v0::GetBlobResponse>();
  privacysandbox::apis::parc::v0::GetBlobRequest get_blob_request_one;
  get_blob_request_one.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  get_blob_request_one.mutable_blob_metadata()->set_blob_name(kSampleBlobName);

  EXPECT_CALL(*mock_reader_one, Read(_)).WillOnce(Return(false));
  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_one)))
      .WillOnce(Return(mock_reader_one));
  EXPECT_CALL(*mock_reader_one, Finish())
      .WillOnce(Return(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                    "Invalid arguement")));

  std::unique_ptr<BlobStorageClient> parc_client =
      std::make_unique<ParcBlobStorageClient>(
          ParcBlobStorageClient(std::move(parc_client_stub)));

  BlobFetcher bucket_fetcher(kSampleBucketName, executor_.get(),
                             std::move(parc_client));
  auto status = bucket_fetcher.FetchSync();
  EXPECT_FALSE(status.ok());
}

TEST(ComputeChecksumForBlobsTest, EmptyBlobsShouldReturnError) {
  std::vector<BlobFetcherBase::BlobView> empty_blobs = {};

  absl::StatusOr<std::string> checksum = ComputeChecksumForBlobs(empty_blobs);

  ASSERT_FALSE(checksum.ok());
  EXPECT_EQ(checksum.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ComputeChecksumForBlobsTest, CanComputeChecksumOnSingleBlob) {
  std::vector<BlobFetcherBase::BlobView> single_blob = {
      BlobFetcherBase::BlobView{.path = "file1.txt", .bytes = "content1"}};

  absl::StatusOr<std::string> checksum = ComputeChecksumForBlobs(single_blob);

  ASSERT_TRUE(checksum.ok());
  EXPECT_EQ(*checksum,
            "4f2d6937ca0d91126b175de2d90138a1c8825a3eb13c2d999317fd1a5f320653");
}

TEST(ComputeChecksumForBlobsTest, ChecksumOnMultipleBlobsCanCommute) {
  BlobFetcherBase::BlobView blob1{.path = "file3.txt", .bytes = "content3"};
  BlobFetcherBase::BlobView blob2{.path = "file1.txt", .bytes = "content1"};
  BlobFetcherBase::BlobView blob3{.path = "file2.txt", .bytes = "content2"};

  // Ascending order.
  std::vector<BlobFetcherBase::BlobView> blobs1 = {blob1, blob2, blob3};
  // Non-ascending order.
  std::vector<BlobFetcherBase::BlobView> blobs2 = {blob2, blob1, blob3};

  absl::StatusOr<std::string> checksum1 = ComputeChecksumForBlobs(blobs1);
  absl::StatusOr<std::string> checksum2 = ComputeChecksumForBlobs(blobs2);

  ASSERT_TRUE(checksum1.ok());
  ASSERT_TRUE(checksum2.ok());
  EXPECT_EQ(*checksum1, *checksum2);
  EXPECT_EQ(*checksum1,
            "4f23a79b280289568bde060cd83efa0088ac765ae65350a43053bab82de9eb40");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
