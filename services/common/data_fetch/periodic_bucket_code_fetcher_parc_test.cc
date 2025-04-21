/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service_mock.grpc.pb.h>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_parc.h"
#include "services/common/blob_storage_client/blob_storage_client_parc_utils.h"
#include "services/common/data_fetch/periodic_bucket_code_fetcher.h"
#include "services/common/data_fetch/version_util.h"
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

constexpr char kSampleBucketName[] = "BucketName";

constexpr char kSampleBlobName[] = "BlobName1";
constexpr char kSampleBlobName2[] = "BlobName2";
constexpr char kSampleBlobName3[] = "BlobName3";
constexpr char kDeeplyNestedBlobName[] = "dir1/dir2/dir3/NestedBlob";

constexpr char kSampleData[] = "test1";
constexpr char kSampleData2[] = "test2";
constexpr char kSampleData3[] = "test3";

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

constexpr absl::Duration kFetchPeriod = absl::Seconds(3);

class ParcPeriodicBucketCodeFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(ParcPeriodicBucketCodeFetcherTest, LoadsWrappedResultIntoCodeLoader) {
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();

  std::string wrapper_string = "_wrapper_";
  auto wrapper = [&wrapper_string](const std::vector<std::string>& blobs) {
    return absl::StrCat(blobs.at(0), wrapper_string);
  };
  // Set listblobmetadata mock.
  BlobMetadata md;
  md.set_bucket_name(kSampleBucketName);
  md.set_blob_name(kSampleBlobName);

  ListBlobsMetadataRequest list_blob_metadata_request;
  ListBlobsMetadataResponse list_blob_metadata_response;
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md));
  EXPECT_CALL(*parc_client_stub,
              ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response),
          Return(grpc::Status::OK)));

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce([](const absl::Duration duration,
                   absl::AnyInvocable<void()> closure) {
        EXPECT_EQ(kFetchPeriod, duration);
        server_common::TaskId id;
        return id;
      });
  EXPECT_CALL(*executor, Cancel).WillOnce(Return(true));

  // Set GetBlob mock
  auto* mock_reader = new MockClientReader<GetBlobResponse>();
  GetBlobRequest get_blob_request;
  GetBlobResponse get_blob_response;
  get_blob_request.mutable_blob_metadata()->set_bucket_name(kSampleBucketName);
  get_blob_request.mutable_blob_metadata()->set_blob_name(kSampleBlobName);
  get_blob_response.set_data(kSampleData);

  EXPECT_CALL(*mock_reader, Read(_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(get_blob_response),
                                 Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*parc_client_stub, GetBlobRaw(_, _))
      .WillOnce(Return(mock_reader));
  EXPECT_CALL(*mock_reader, Finish()).WillOnce(Return(Status::OK));

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&wrapper_string](std::string_view version,
                                  absl::string_view blob_data) {
        EXPECT_EQ(blob_data, absl::StrCat(kSampleData, wrapper_string));
        return absl::OkStatus();
      });

  std::unique_ptr<BlobStorageClient> parc_client =
      std::make_unique<ParcBlobStorageClient>(
          ParcBlobStorageClient(std::move(parc_client_stub)));

  PeriodicBucketCodeFetcher bucket_fetcher(
      kSampleBucketName, kFetchPeriod, &dispatcher, executor.get(),
      std::move(wrapper), parc_client.get());
  auto status = bucket_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  bucket_fetcher.End();
}

TEST_F(ParcPeriodicBucketCodeFetcherTest, LoadsAllBlobsInBucket) {
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();
  // Set listblobmetadata mock.
  BlobMetadata md;
  md.set_bucket_name(kSampleBucketName);
  md.set_blob_name(kSampleBlobName);
  BlobMetadata md2;
  md2.set_bucket_name(std::string(kSampleBucketName));
  md2.set_blob_name(std::string(kDeeplyNestedBlobName));

  ListBlobsMetadataRequest list_blob_metadata_request;
  ListBlobsMetadataResponse list_blob_metadata_response;
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md));
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md2));
  EXPECT_CALL(*parc_client_stub,
              ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response),
          Return(grpc::Status::OK)));

  auto wrapper = [](const std::vector<std::string>& blobs) {
    return blobs.at(0);
  };

  // Set GetBlob mock
  auto* mock_reader_one = new MockClientReader<GetBlobResponse>();
  GetBlobRequest get_blob_request_one;
  GetBlobResponse get_blob_response_one;
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

  auto* mock_reader_two = new MockClientReader<GetBlobResponse>();
  GetBlobRequest get_blob_request_two;
  GetBlobResponse get_blob_response_two;
  get_blob_request_two.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  get_blob_request_two.mutable_blob_metadata()->set_blob_name(
      kDeeplyNestedBlobName);
  get_blob_response_two.set_data(kSampleData2);

  EXPECT_CALL(*mock_reader_two, Read(_))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<0>(get_blob_response_two), Return(true)))
      .WillOnce(Return(false));

  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_two)))
      .WillOnce(Return(mock_reader_two));
  EXPECT_CALL(*mock_reader_two, Finish()).WillOnce(Return(Status::OK));

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            server_common::TaskId id;
            return id;
          });
  EXPECT_CALL(*executor, Cancel).WillOnce(Return(true));

  const absl::StatusOr<std::string> versionA =
      GetBucketBlobVersion(kSampleBucketName, kSampleBlobName);
  ASSERT_TRUE(versionA.ok()) << versionA.status();
  const absl::StatusOr<std::string> versionB =
      GetBucketBlobVersion(kSampleBucketName, kDeeplyNestedBlobName);
  ASSERT_TRUE(versionB.ok()) << versionB.status();

  EXPECT_CALL(dispatcher, LoadSync(*versionA, kSampleData))
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        return absl::OkStatus();
      });

  EXPECT_CALL(dispatcher, LoadSync(*versionB, kSampleData2))
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        return absl::OkStatus();
      });

  std::unique_ptr<BlobStorageClient> parc_client =
      std::make_unique<ParcBlobStorageClient>(
          ParcBlobStorageClient(std::move(parc_client_stub)));

  PeriodicBucketCodeFetcher bucket_fetcher(
      kSampleBucketName, kFetchPeriod, &dispatcher, executor.get(),
      std::move(wrapper), parc_client.get());
  auto status = bucket_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  bucket_fetcher.End();
}

TEST_F(ParcPeriodicBucketCodeFetcherTest, ReturnsSuccessIfAtLeastOneBlobLoads) {
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();

  auto wrapper = [](const std::vector<std::string>& blobs) {
    return blobs.at(0);
  };

  // Set listblobmetadata mock.
  BlobMetadata md;
  md.set_bucket_name(kSampleBucketName);
  md.set_blob_name(kSampleBlobName);
  BlobMetadata md2;
  md2.set_bucket_name(std::string(kSampleBucketName));
  md2.set_blob_name(std::string(kSampleBlobName2));
  BlobMetadata md3;
  md3.set_bucket_name(std::string(kSampleBucketName));
  md3.set_blob_name(std::string(kSampleBlobName3));

  ListBlobsMetadataRequest list_blob_metadata_request;
  ListBlobsMetadataResponse list_blob_metadata_response;
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md));
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md2));
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md3));

  EXPECT_CALL(*parc_client_stub,
              ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response),
          Return(grpc::Status::OK)));

  // Set GetBlob mock
  auto* mock_reader_one = new MockClientReader<GetBlobResponse>();
  auto* mock_reader_two = new MockClientReader<GetBlobResponse>();
  auto* mock_reader_three = new MockClientReader<GetBlobResponse>();
  GetBlobRequest get_blob_request_two;
  GetBlobResponse get_blob_response_two;
  get_blob_request_two.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  get_blob_request_two.mutable_blob_metadata()->set_blob_name(kSampleBlobName2);
  get_blob_response_two.set_data(kSampleData2);

  GetBlobRequest get_blob_request_one;
  get_blob_request_one.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  get_blob_request_one.mutable_blob_metadata()->set_blob_name(kSampleBlobName);
  GetBlobRequest get_blob_request_three;
  get_blob_request_three.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  get_blob_request_three.mutable_blob_metadata()->set_blob_name(
      kSampleBlobName3);

  // Set success read.
  EXPECT_CALL(*mock_reader_two, Read(_))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<0>(get_blob_response_two), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_two)))
      .WillOnce(Return(mock_reader_two));
  EXPECT_CALL(*mock_reader_two, Finish()).WillOnce(Return(Status::OK));

  // Set fail read.
  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_one)))
      .WillOnce(Return(mock_reader_one));
  EXPECT_CALL(*mock_reader_one, Read(_)).WillOnce(Return(false));
  EXPECT_CALL(*mock_reader_one, Finish())
      .WillOnce(
          Return(grpc::Status(grpc::StatusCode::DATA_LOSS, "Read failed")));

  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_three)))
      .WillOnce(Return(mock_reader_three));
  EXPECT_CALL(*mock_reader_three, Read(_)).WillOnce(Return(false));
  EXPECT_CALL(*mock_reader_three, Finish())
      .WillOnce(Return(
          grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid input")));

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            server_common::TaskId id;
            return id;
          });
  EXPECT_CALL(*executor, Cancel).WillOnce(Return(true));

  absl::StatusOr<std::string> versionA =
      GetBucketBlobVersion(kSampleBucketName, kSampleBlobName);
  ASSERT_TRUE(versionA.ok()) << versionA.status();
  absl::StatusOr<std::string> versionB =
      GetBucketBlobVersion(kSampleBucketName, kSampleBlobName2);
  ASSERT_TRUE(versionB.ok()) << versionB.status();

  EXPECT_CALL(dispatcher, LoadSync(*versionA, kSampleData)).Times(0);

  EXPECT_CALL(dispatcher, LoadSync(*versionB, kSampleData2))
      .Times(1)
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        return absl::OkStatus();
      });

  EXPECT_CALL(dispatcher, LoadSync(*versionA, kSampleData3)).Times(0);

  std::unique_ptr<BlobStorageClient> parc_client =
      std::make_unique<ParcBlobStorageClient>(
          ParcBlobStorageClient(std::move(parc_client_stub)));

  PeriodicBucketCodeFetcher bucket_fetcher(
      kSampleBucketName, kFetchPeriod, &dispatcher, executor.get(),
      std::move(wrapper), parc_client.get());
  auto status = bucket_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  bucket_fetcher.End();
}

TEST_F(ParcPeriodicBucketCodeFetcherTest,
       FailsStartupIfNoBlobLoadedSuccessfully) {
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();

  auto wrapper = [](const std::vector<std::string>& blobs) {
    return blobs.at(0);
  };

  // Set listblobmetadata mock.
  BlobMetadata md;
  md.set_bucket_name(kSampleBucketName);
  md.set_blob_name(kSampleBlobName);

  ListBlobsMetadataRequest list_blob_metadata_request;
  ListBlobsMetadataResponse list_blob_metadata_response;
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      kSampleBucketName);
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md));
  EXPECT_CALL(*parc_client_stub,
              ListBlobsMetadata(_, EqualsProto(list_blob_metadata_request), _))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<2>(list_blob_metadata_response),
          Return(grpc::Status::OK)));

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce([](const absl::Duration duration,
                   absl::AnyInvocable<void()> closure) {
        EXPECT_EQ(kFetchPeriod, duration);
        server_common::TaskId id;
        return id;
      });
  EXPECT_CALL(*executor, Cancel).WillOnce(Return(true));

  // Set GetBlob mock
  auto* mock_reader = new MockClientReader<GetBlobResponse>();
  GetBlobRequest get_blob_request;
  GetBlobResponse get_blob_response;
  get_blob_request.mutable_blob_metadata()->set_bucket_name(kSampleBucketName);
  get_blob_request.mutable_blob_metadata()->set_blob_name(kSampleBlobName);
  get_blob_response.set_data(kSampleData);

  EXPECT_CALL(*mock_reader, Read(_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(get_blob_response),
                                 Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*parc_client_stub, GetBlobRaw(_, _))
      .WillOnce(Return(mock_reader));
  EXPECT_CALL(*mock_reader, Finish()).WillOnce(Return(Status::OK));

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(blob_data, kSampleData);
        return absl::UnavailableError("blob invalid");
      });
  std::unique_ptr<BlobStorageClient> parc_client =
      std::make_unique<ParcBlobStorageClient>(
          ParcBlobStorageClient(std::move(parc_client_stub)));

  PeriodicBucketCodeFetcher bucket_fetcher(
      kSampleBucketName, kFetchPeriod, &dispatcher, executor.get(),
      std::move(wrapper), parc_client.get());
  EXPECT_FALSE(bucket_fetcher.Start().ok());
  bucket_fetcher.End();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
