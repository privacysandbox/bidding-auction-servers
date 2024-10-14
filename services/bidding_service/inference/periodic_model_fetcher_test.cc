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

#include "services/bidding_service/inference/periodic_model_fetcher.h"

#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "proto/inference_sidecar.pb.h"
#include "proto/inference_sidecar_mock.grpc.pb.h"
#include "services/bidding_service/inference/inference_flags.h"
#include "services/common/blob_fetch/blob_fetcher_mock.h"
#include "services/common/test/mocks.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::Duration kFetchPeriod = absl::Seconds(1);
constexpr char kTestModelName1[] = "model1";
constexpr char kTestModelContent1[] = "bytes1";
constexpr char kTestModelName2[] = "model2";
constexpr char kTestModelContent2[] = "bytes2";
constexpr char kModelConfigPath[] = "model_metadata_config.json";
constexpr char kModelWarmUpRequestJson[] = "model_warm_up_request_json";

using ::google::scp::core::test::EqualsProto;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::ReturnRef;

class PeriodicModelFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<google::protobuf::Message>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
  }
};

TEST_F(PeriodicModelFetcherTest, FetchesAndRegistersAllModels) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1"},
            {"model_path": "model2"}
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1),
      BlobFetcherBase::Blob(kTestModelName2, kTestModelContent2)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(1);
  {
    InSequence s;

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kModelConfigPath))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot())
        .WillOnce(ReturnRef(config_fetch_result));

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kTestModelName1, kTestModelName2))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot()).WillOnce(ReturnRef(mock_snapshot));
  }

  RegisterModelRequest register_model_request_1;
  register_model_request_1.mutable_model_spec()->set_model_path(
      kTestModelName1);
  (*register_model_request_1.mutable_model_files())[kTestModelName1] =
      kTestModelContent1;

  RegisterModelRequest register_model_request_2;
  register_model_request_2.mutable_model_spec()->set_model_path(
      kTestModelName2);
  (*register_model_request_2.mutable_model_files())[kTestModelName2] =
      kTestModelContent2;

  EXPECT_CALL(*mock_inference_stub,
              RegisterModel(_, EqualsProto(register_model_request_1), _))
      .WillOnce(Return(grpc::Status::OK));

  EXPECT_CALL(*mock_inference_stub,
              RegisterModel(_, EqualsProto(register_model_request_2), _))
      .WillOnce(Return(grpc::Status::OK));

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [&done](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            done.DecrementCount();
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  model_fetcher.End();
}

TEST_F(PeriodicModelFetcherTest, ShouldNotRegisterSameModelTwice) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1"},
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  // Triggers periodic model fetching twice.
  absl::BlockingCounter done(2);

  {
    InSequence s;

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kModelConfigPath))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot())
        .WillOnce(ReturnRef(config_fetch_result));

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kTestModelName1))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot()).WillOnce(ReturnRef(mock_snapshot));

    // Second fetch gets the same model config.
    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kModelConfigPath))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot())
        .WillOnce(ReturnRef(config_fetch_result));
  }

  EXPECT_CALL(*mock_inference_stub, RegisterModel)
      .WillOnce(Return(grpc::Status::OK));

  EXPECT_CALL(*executor, RunAfter)
      .Times(2)
      .WillRepeatedly(
          [&done](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            if (!done.DecrementCount()) {
              closure();
            }
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  model_fetcher.End();
}

TEST_F(PeriodicModelFetcherTest,
       GetModelMetadataConfigFailureShouldNotRegisterModel) {
  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(1);

  EXPECT_CALL(
      *blob_fetcher,
      FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                      ElementsAre(kModelConfigPath))))
      .WillOnce(Return(absl::UnknownError("Unknown Error")));

  EXPECT_CALL(*mock_inference_stub, RegisterModel).Times(0);

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [&done](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            done.DecrementCount();
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  model_fetcher.End();
}

TEST_F(PeriodicModelFetcherTest, CloudFetchFailureShouldNotRegisterModel) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1"}
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(1);

  {
    InSequence s;

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kModelConfigPath))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot())
        .WillOnce(ReturnRef(config_fetch_result));

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        testing::ElementsAre(kTestModelName1))))
        .WillOnce(Return(absl::UnknownError("Unknown Error")));
  }

  EXPECT_CALL(*mock_inference_stub, RegisterModel).Times(0);

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [&done](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            done.DecrementCount();
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  model_fetcher.End();
}

TEST_F(PeriodicModelFetcherTest, IncorrectChecksumShouldNotRegisterModel) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1",
             "checksum": "aaa"}
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(1);

  {
    InSequence s;

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kModelConfigPath))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot())
        .WillOnce(ReturnRef(config_fetch_result));

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        testing::ElementsAre(kTestModelName1))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot()).WillOnce(ReturnRef(mock_snapshot));
  }

  EXPECT_CALL(*mock_inference_stub, RegisterModel).Times(0);

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [&done](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            done.DecrementCount();
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  model_fetcher.End();
}

TEST_F(PeriodicModelFetcherTest, CorrectChecksumShouldRegisterModel) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1",
             "checksum": "59390c4fa97b2cbfe5bd300e254e6eddb7b4b45c2dbc0c1cfc6e93793a143d04"}
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(1);
  {
    InSequence s;

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kModelConfigPath))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot())
        .WillOnce(ReturnRef(config_fetch_result));

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kTestModelName1))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot()).WillOnce(ReturnRef(mock_snapshot));
  }

  RegisterModelRequest register_model_request;
  register_model_request.mutable_model_spec()->set_model_path(kTestModelName1);
  (*register_model_request.mutable_model_files())[kTestModelName1] =
      kTestModelContent1;

  EXPECT_CALL(*mock_inference_stub,
              RegisterModel(_, EqualsProto(register_model_request), _))
      .WillOnce(Return(grpc::Status::OK));

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [&done](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            done.DecrementCount();
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  model_fetcher.End();
}

TEST_F(PeriodicModelFetcherTest, RegisterModelWithWarmUpRequestIfProvided) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1"},
            {"model_path": "model2", "warm_up_batch_request_json": "model_warm_up_request_json"}
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1),
      BlobFetcherBase::Blob(kTestModelName2, kTestModelContent2)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(1);
  {
    InSequence s;

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kModelConfigPath))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot())
        .WillOnce(ReturnRef(config_fetch_result));

    EXPECT_CALL(
        *blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAre(kTestModelName1, kTestModelName2))))
        .WillOnce(Return(absl::OkStatus()));

    EXPECT_CALL(*blob_fetcher, snapshot()).WillOnce(ReturnRef(mock_snapshot));
  }

  RegisterModelRequest register_model_request_1;
  register_model_request_1.mutable_model_spec()->set_model_path(
      kTestModelName1);
  (*register_model_request_1.mutable_model_files())[kTestModelName1] =
      kTestModelContent1;

  RegisterModelRequest register_model_request_2;
  register_model_request_2.mutable_model_spec()->set_model_path(
      kTestModelName2);
  (*register_model_request_2.mutable_model_files())[kTestModelName2] =
      kTestModelContent2;
  register_model_request_2.set_warm_up_batch_request_json(
      kModelWarmUpRequestJson);

  EXPECT_CALL(*mock_inference_stub,
              RegisterModel(_, EqualsProto(register_model_request_1), _))
      .WillOnce(Return(grpc::Status::OK));

  EXPECT_CALL(*mock_inference_stub,
              RegisterModel(_, EqualsProto(register_model_request_2), _))
      .WillOnce(Return(grpc::Status::OK));

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [&done](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            done.DecrementCount();
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  model_fetcher.End();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
