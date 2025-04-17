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
#include <string>
#include <utility>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service_mock.grpc.pb.h>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/bidding_service/cddl_spec_cache.h"
#include "services/bidding_service/egress_features/egress_schema_bucket_fetcher.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_parc.h"
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

constexpr absl::string_view kTestBucket = "test";
constexpr absl::string_view kSchema1BlobName = "schema1.json";
constexpr absl::string_view kSchema2BlobName = "schema2.json";
constexpr absl::string_view kSchema3BlobName = "schema3.json";

constexpr char kSchema[] = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "bucket-feature",
          "size": 2
        }
      ]
    }
  )JSON";

class ParcEgressSchemaBucketFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    auto cddl_spec_cache = std::make_unique<CddlSpecCache>(
        "services/bidding_service/egress_cddl_spec/");
    CHECK_OK(cddl_spec_cache->Init());
    egress_schema_cache_ =
        std::make_unique<EgressSchemaCache>(std::move(cddl_spec_cache));

    EXPECT_CALL(*executor_, RunAfter)
        .WillOnce(
            [](const absl::Duration duration, absl::AnyInvocable<void()> cb) {
              server_common::TaskId id;
              return id;
            });
    EXPECT_CALL(*executor_, Cancel).WillOnce(Return(true));
  }
  std::unique_ptr<MockExecutor> executor_ = std::make_unique<MockExecutor>();
  std::unique_ptr<EgressSchemaCache> egress_schema_cache_;
};

TEST_F(ParcEgressSchemaBucketFetcherTest, UpdatesCacheWithSuccessfulFetches) {
  std::shared_ptr<MockParcServiceStub> parc_client_stub =
      std::make_unique<MockParcServiceStub>();

  // Set listblobmetadata mock.
  BlobMetadata md1;
  md1.set_bucket_name(kTestBucket);
  md1.set_blob_name(kSchema1BlobName);
  BlobMetadata md2;
  md2.set_bucket_name(kTestBucket);
  md2.set_blob_name(kSchema2BlobName);
  BlobMetadata md3;
  md3.set_bucket_name(kTestBucket);
  md3.set_blob_name(kSchema3BlobName);

  ListBlobsMetadataRequest list_blob_metadata_request;
  ListBlobsMetadataResponse list_blob_metadata_response;
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      kTestBucket);
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_response.mutable_blob_metadatas()->Add(std::move(md1));
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

  GetBlobRequest get_blob_request_one;
  get_blob_request_one.mutable_blob_metadata()->set_bucket_name(kTestBucket);
  get_blob_request_one.mutable_blob_metadata()->set_blob_name(kSchema1BlobName);
  // Set fail read.
  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_one)))
      .WillOnce(Return(mock_reader_one));
  EXPECT_CALL(*mock_reader_one, Read(_)).WillOnce(Return(false));
  EXPECT_CALL(*mock_reader_one, Finish())
      .WillOnce(Return(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                    "Invalid arguement")));

  // Set success read.
  GetBlobRequest get_blob_request_two;
  GetBlobResponse get_blob_response_two;
  get_blob_request_two.mutable_blob_metadata()->set_bucket_name(kTestBucket);
  get_blob_request_two.mutable_blob_metadata()->set_blob_name(kSchema2BlobName);
  get_blob_response_two.set_data(kSchema);
  EXPECT_CALL(*mock_reader_two, Read(_))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<0>(get_blob_response_two), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_two)))
      .WillOnce(Return(mock_reader_two));
  EXPECT_CALL(*mock_reader_two, Finish()).WillOnce(Return(Status::OK));

  GetBlobRequest get_blob_request_three;
  GetBlobResponse get_blob_response_three;
  get_blob_request_three.mutable_blob_metadata()->set_bucket_name(kTestBucket);
  get_blob_request_three.mutable_blob_metadata()->set_blob_name(
      kSchema3BlobName);
  get_blob_response_three.set_data(kSchema);
  EXPECT_CALL(*mock_reader_three, Read(_))
      .WillOnce(::testing::DoAll(
          ::testing::SetArgPointee<0>(get_blob_response_three), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*parc_client_stub,
              GetBlobRaw(_, EqualsProto(get_blob_request_three)))
      .WillOnce(Return(mock_reader_three));
  EXPECT_CALL(*mock_reader_three, Finish()).WillOnce(Return(Status::OK));

  std::unique_ptr<BlobStorageClient> parc_client =
      std::make_unique<ParcBlobStorageClient>(
          ParcBlobStorageClient(std::move(parc_client_stub)));

  std::unique_ptr<EgressSchemaBucketFetcher> fetcher =
      std::make_unique<EgressSchemaBucketFetcher>(
          kTestBucket, absl::Milliseconds(1000), executor_.get(),
          parc_client.get(), egress_schema_cache_.get());

  auto fetch_status = fetcher->Start();
  ASSERT_TRUE(fetch_status.ok()) << fetch_status;
  fetcher->End();
  auto first_schema = egress_schema_cache_->Get(
      *GetBucketBlobVersion(kTestBucket, kSchema1BlobName));
  ASSERT_EQ(first_schema.status().code(), absl::StatusCode::kInvalidArgument);
  auto second_schema = egress_schema_cache_->Get(
      *GetBucketBlobVersion(kTestBucket, kSchema2BlobName));
  ASSERT_TRUE(second_schema.ok());
  ASSERT_EQ(second_schema->version, 2);
  auto third_schema = egress_schema_cache_->Get(
      *GetBucketBlobVersion(kTestBucket, kSchema3BlobName));
  ASSERT_TRUE(third_schema.ok());
  ASSERT_EQ(third_schema->version, 2);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
