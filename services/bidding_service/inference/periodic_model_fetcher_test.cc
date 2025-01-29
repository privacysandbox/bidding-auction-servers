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

constexpr absl::Duration kFetchPeriod = absl::Milliseconds(60001);
constexpr char kTestModelName1[] = "model1";
constexpr char kTestModelContent1[] = "bytes1";
constexpr char kTestModelName2[] = "model2";
constexpr char kTestModelContent2[] = "bytes2";
constexpr char kModelConfigPath[] = "model_metadata_config.json";
constexpr char kModelWarmUpRequestJson[] = "model_warm_up_request_json";

using ::google::scp::core::test::EqualsProto;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
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

  void SetUpCloudFetchExpectation(
      const std::vector<absl::string_view>& paths,
      const std::vector<BlobFetcherBase::Blob>& fetch_result,
      BlobFetcherMock& blob_fetcher) {
    EXPECT_CALL(
        blob_fetcher,
        FetchSync(Field(&BlobFetcherBase::FilterOptions::included_prefixes,
                        ElementsAreArray(paths))))
        .WillOnce(Return(absl::OkStatus()));
    EXPECT_CALL(blob_fetcher, snapshot()).WillOnce(ReturnRef(fetch_result));
  }

  void SetUpRegisterModelExpectation(absl::string_view model_path,
                                     absl::string_view model_content,
                                     MockInferenceServiceStub& inference_stub) {
    SetUpRegisterModelExpectation(model_path, model_content, "",
                                  inference_stub);
  }

  void SetUpRegisterModelExpectation(absl::string_view model_path,
                                     absl::string_view model_content,
                                     absl::string_view warm_up_request_json,
                                     MockInferenceServiceStub& inference_stub) {
    RegisterModelRequest request;
    request.mutable_model_spec()->set_model_path(model_path);
    (*request.mutable_model_files())[model_path] = model_content;
    if (!warm_up_request_json.empty()) {
      request.set_warm_up_batch_request_json(warm_up_request_json);
    }

    EXPECT_CALL(inference_stub, RegisterModel(_, EqualsProto(request), _))
        .WillOnce(Return(grpc::Status::OK));
  }

  void SetupDeleteModelExpectation(absl::string_view model_path,
                                   MockInferenceServiceStub& inference_stub,
                                   bool succeeded = true) {
    DeleteModelRequest delete_model_request;
    delete_model_request.mutable_model_spec()->set_model_path(model_path);
    grpc::Status grpc_status =
        succeeded ? grpc::Status::OK
                  : grpc::Status(grpc::StatusCode::INTERNAL, "sidecar error");
    EXPECT_CALL(inference_stub,
                DeleteModel(_, EqualsProto(delete_model_request), _))
        .WillOnce(Return(grpc_status));
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
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1, kTestModelName2},
                               mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);
    SetUpRegisterModelExpectation(kTestModelName2, kTestModelContent2,
                                  *mock_inference_stub);
  }

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
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);

    // Second fetch gets the same model config but doesn't register any model.
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
  }

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
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
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
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
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
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);
  }

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

TEST_F(PeriodicModelFetcherTest, UpdatedChecksumShouldTriggerModelFetch) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1"},
        ]
  })";

  const std::string model_metadata_config_updated_checksum = R"({
        "model_metadata": [
            {"model_path": "model1",
             "checksum": "59390c4fa97b2cbfe5bd300e254e6eddb7b4b45c2dbc0c1cfc6e93793a143d04"}
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  const std::vector<BlobFetcherBase::Blob>
      config_fetch_result_updated_checksum = {BlobFetcherBase::Blob(
          kModelConfigPath, model_metadata_config_updated_checksum)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  // Triggers periodic model fetching twice.
  absl::BlockingCounter done(2);

  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);

    // Second fetch gets the model at the same path but different checksums.
    // In this case, it's the same model but initially the model's checksum is
    // an empty string.
    SetUpCloudFetchExpectation({kModelConfigPath},
                               config_fetch_result_updated_checksum,
                               *blob_fetcher);
    SetupDeleteModelExpectation(kTestModelName1, *mock_inference_stub);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);
  }

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

TEST_F(PeriodicModelFetcherTest, DeleteModelSuccess) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1"},
        ]
  })";

  const std::string empty_model_metadata_config = R"({
        "model_metadata": []
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> empty_config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, empty_model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  // Triggers periodic model fetching twice.
  absl::BlockingCounter done(2);

  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);

    // Second poll gets an empty config.
    SetUpCloudFetchExpectation({kModelConfigPath}, empty_config_fetch_result,
                               *blob_fetcher);
    SetupDeleteModelExpectation(kTestModelName1, *mock_inference_stub);
  }

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

TEST_F(PeriodicModelFetcherTest, DeleteFailureShouldRetry) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1"},
        ]
  })";

  const std::string empty_model_metadata_config = R"({
        "model_metadata": []
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> empty_config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, empty_model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  // Triggers periodic model fetching three times.
  absl::BlockingCounter done(3);

  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);

    // Second and third polls get an empty config.
    SetUpCloudFetchExpectation({kModelConfigPath}, empty_config_fetch_result,
                               *blob_fetcher);
    SetupDeleteModelExpectation(kTestModelName1, *mock_inference_stub,
                                /**succeeded=*/false);

    SetUpCloudFetchExpectation({kModelConfigPath}, empty_config_fetch_result,
                               *blob_fetcher);
    SetupDeleteModelExpectation(kTestModelName1, *mock_inference_stub,
                                /**succeeded=*/true);
  }

  EXPECT_CALL(*executor, RunAfter)
      .Times(3)
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

TEST_F(PeriodicModelFetcherTest, UpdateModelVersionSuccess) {
  const std::string model_metadata_config_v1 = R"({
        "model_metadata": [
            {"model_path": "model1"},
        ]
  })";

  const std::string model_metadata_config_v2 = R"({
        "model_metadata": [
            {"model_path": "model2"},
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_v1_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config_v1)};

  const std::vector<BlobFetcherBase::Blob> config_v2_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config_v2)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot_v1 = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot_v2 = {
      BlobFetcherBase::Blob(kTestModelName2, kTestModelContent2)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  // Triggers periodic model fetching twice.
  absl::BlockingCounter done(2);

  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_v1_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot_v1,
                               *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);

    // Second polling of the model configuration file.
    SetUpCloudFetchExpectation({kModelConfigPath}, config_v2_fetch_result,
                               *blob_fetcher);
    SetupDeleteModelExpectation(kTestModelName1, *mock_inference_stub);
    SetUpCloudFetchExpectation({kTestModelName2}, mock_snapshot_v2,
                               *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName2, kTestModelContent2,
                                  *mock_inference_stub);
  }

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

TEST_F(PeriodicModelFetcherTest, DeleteModelWithGracePeriodSuccess) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1", "eviction_grace_period_in_ms": 500},
        ]
  })";

  const std::string empty_model_metadata_config = R"({
        "model_metadata": []
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> empty_config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, empty_model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  // Triggers periodic model fetching twice.
  absl::BlockingCounter done(2);
  absl::BlockingCounter eviction_worker_done(1);

  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);

    // Second poll gets an empty config.
    SetUpCloudFetchExpectation({kModelConfigPath}, empty_config_fetch_result,
                               *blob_fetcher);
    SetupDeleteModelExpectation(kTestModelName1, *mock_inference_stub);
  }

  EXPECT_CALL(*executor, RunAfter)
      .WillRepeatedly(
          [&done, &eviction_worker_done](absl::Duration duration,
                                         absl::AnyInvocable<void()> closure) {
            if (duration == kFetchPeriod) {
              // Executor being invoked by the model fetching callback.
              if (!done.DecrementCount()) {
                closure();
              }
            } else {
              // Executor being invoked by the eviction callback.
              std::thread([&eviction_worker_done,
                           closure = std::move(closure)]() mutable {
                closure();
                eviction_worker_done.DecrementCount();
              }).detach();
            }
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  eviction_worker_done.Wait();
  model_fetcher.End();
}

TEST_F(PeriodicModelFetcherTest, LongGracePeriodShouldOnlyDeleteOnce) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1", "eviction_grace_period_in_ms": 2000},
        ]
  })";

  const std::string empty_model_metadata_config = R"({
        "model_metadata": []
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> empty_config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, empty_model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  // Triggers periodic model fetching three times.
  absl::BlockingCounter done(3);
  absl::BlockingCounter eviction_worker_done(1);

  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);

    // Second and third polls get an empty config.
    SetUpCloudFetchExpectation({kModelConfigPath}, empty_config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kModelConfigPath}, empty_config_fetch_result,
                               *blob_fetcher);
    SetupDeleteModelExpectation(kTestModelName1, *mock_inference_stub);
  }

  EXPECT_CALL(*executor, RunAfter)
      .WillRepeatedly(
          [&done, &eviction_worker_done](absl::Duration duration,
                                         absl::AnyInvocable<void()> closure) {
            if (duration == kFetchPeriod) {
              // Executor being invoked by the model fetching callback.
              if (!done.DecrementCount()) {
                closure();
              }
            } else {
              // Executor being invoked by the eviction callback.
              std::thread([&done, &eviction_worker_done,
                           closure = std::move(closure)]() mutable {
                done.Wait();
                closure();
                eviction_worker_done.DecrementCount();
              }).detach();
            }
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  eviction_worker_done.Wait();
  model_fetcher.End();
}

TEST_F(PeriodicModelFetcherTest,
       ModelOverwriteWithGracePeriodShouldDelayModelFetching) {
  const std::string model_metadata_config = R"({
        "model_metadata": [
            {"model_path": "model1", "eviction_grace_period_in_ms": 500},
        ]
  })";

  const std::string model_metadata_config_updated_checksum = R"({
        "model_metadata": [
            {"model_path": "model1",
             "checksum": "59390c4fa97b2cbfe5bd300e254e6eddb7b4b45c2dbc0c1cfc6e93793a143d04"}
        ]
  })";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> updated_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath,
                            model_metadata_config_updated_checksum)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(kTestModelName1, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(2);
  absl::BlockingCounter eviction_worker_done(1);

  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);

    // Model fetching doesn't happen because of grace period.
    SetUpCloudFetchExpectation({kModelConfigPath}, updated_fetch_result,
                               *blob_fetcher);
    SetupDeleteModelExpectation(kTestModelName1, *mock_inference_stub);
  }

  EXPECT_CALL(*executor, RunAfter)
      .WillRepeatedly(
          [&done, &eviction_worker_done](absl::Duration duration,
                                         absl::AnyInvocable<void()> closure) {
            if (duration == kFetchPeriod) {
              // Executor being invoked by the model fetching callback.
              if (!done.DecrementCount()) {
                closure();
              }
            } else {
              // Executor being invoked by the eviction callback.
              std::thread([&done, &eviction_worker_done,
                           closure = std::move(closure)]() mutable {
                done.Wait();
                closure();
                eviction_worker_done.DecrementCount();
              }).detach();
            }
            return server_common::TaskId();
          });

  PeriodicModelFetcher model_fetcher(kModelConfigPath, std::move(blob_fetcher),
                                     std::move(mock_inference_stub),
                                     executor.get(), kFetchPeriod);
  auto status = model_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  eviction_worker_done.Wait();
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
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1, kTestModelName2},
                               mock_snapshot, *blob_fetcher);
    SetUpRegisterModelExpectation(kTestModelName1, kTestModelContent1,
                                  *mock_inference_stub);
    SetUpRegisterModelExpectation(kTestModelName2, kTestModelContent2,
                                  kModelWarmUpRequestJson,
                                  *mock_inference_stub);
  }

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

TEST_F(PeriodicModelFetcherTest,
       ModelDirectoryPathShouldRegisterEntireDirectory) {
  const std::string model_metadata_config = R"({
    "model_metadata": [
        {"model_path": "model1/"}
    ]
  })";

  const std::string model_blob_path_1 = "model1/blob1";
  const std::string model_blob_path_2 = "model1/blob2";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(model_blob_path_1, kTestModelContent1),
      BlobFetcherBase::Blob(model_blob_path_2, kTestModelContent2)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(1);
  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({"model1/"}, mock_snapshot, *blob_fetcher);

    RegisterModelRequest request;
    request.mutable_model_spec()->set_model_path("model1/");
    (*request.mutable_model_files())[model_blob_path_1] = kTestModelContent1;
    (*request.mutable_model_files())[model_blob_path_2] = kTestModelContent2;
    EXPECT_CALL(*mock_inference_stub, RegisterModel(_, EqualsProto(request), _))
        .WillOnce(Return(grpc::Status::OK));
  }

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

TEST_F(PeriodicModelFetcherTest, DirectModelPathShouldRegisterSingleModelFile) {
  const std::string model_metadata_config = R"({
    "model_metadata": [
        {"model_path": "model1"}
    ]
  })";

  const std::string model_blob_path_1 = "model1";
  const std::string model_blob_path_2 = "model12";
  const std::string model_blob_path_3 = "model12/v2";

  const std::vector<BlobFetcherBase::Blob> config_fetch_result = {
      BlobFetcherBase::Blob(kModelConfigPath, model_metadata_config)};

  const std::vector<BlobFetcherBase::Blob> mock_snapshot = {
      BlobFetcherBase::Blob(model_blob_path_1, kTestModelContent1),
      BlobFetcherBase::Blob(model_blob_path_2, kTestModelContent1),
      BlobFetcherBase::Blob(model_blob_path_3, kTestModelContent1)};

  auto blob_fetcher = std::make_unique<BlobFetcherMock>();
  auto mock_inference_stub = std::make_unique<MockInferenceServiceStub>();
  auto executor = std::make_unique<MockExecutor>();

  absl::BlockingCounter done(1);
  {
    InSequence s;
    SetUpCloudFetchExpectation({kModelConfigPath}, config_fetch_result,
                               *blob_fetcher);
    SetUpCloudFetchExpectation({kTestModelName1}, mock_snapshot, *blob_fetcher);

    RegisterModelRequest request;
    request.mutable_model_spec()->set_model_path("model1");
    (*request.mutable_model_files())[model_blob_path_1] = kTestModelContent1;
    EXPECT_CALL(*mock_inference_stub, RegisterModel(_, EqualsProto(request), _))
        .WillOnce(Return(grpc::Status::OK));
  }

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
