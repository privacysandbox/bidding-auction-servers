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

#include "model/model_store.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kTestModelName = "test_model";
constexpr absl::string_view kNonExistModelName = "non_exist_model";
constexpr int kNumThreads = 100;

class MockModel {
 public:
  MockModel() : counter_(0) {}
  explicit MockModel(int init_counter) : counter_(init_counter) {}

  void Increment() {
    absl::MutexLock lock(&mutex_);
    counter_++;
  }

  int GetCounter() {
    absl::MutexLock lock(&mutex_);
    return counter_;
  }

 private:
  int counter_;
  mutable absl::Mutex mutex_;
};

absl::StatusOr<std::unique_ptr<MockModel>> MockModelConstructor(
    const InferenceSidecarRuntimeConfig& config,
    const RegisterModelRequest& request,
    ModelConstructMetrics& construct_metrics) {
  return std::make_unique<MockModel>();
}

absl::StatusOr<std::unique_ptr<MockModel>> FailureModelConstructor(
    const InferenceSidecarRuntimeConfig& config,
    const RegisterModelRequest& request,
    ModelConstructMetrics& construct_metrics) {
  return absl::FailedPreconditionError("Error");
}

class MockModelStore : public ModelStore<MockModel> {
 public:
  using ModelStore<MockModel>::ModelStore;
  using ModelStore<MockModel>::PutModel;
  using ModelStore<MockModel>::GetModel;
  using ModelStore<MockModel>::ResetModel;
  using ModelStore<MockModel>::SetModelConstructorForTestOnly;
};

class ModelStoreTest : public ::testing::Test {
 protected:
  ModelStoreTest()
      : store_(InferenceSidecarRuntimeConfig(), MockModelConstructor) {}

  MockModelStore store_;
  ModelConstructMetrics model_construct_metrics_;
};

TEST_F(ModelStoreTest, PutModelAddsModel) {
  RegisterModelRequest request;
  EXPECT_TRUE(
      store_.PutModel(kTestModelName, request, model_construct_metrics_).ok());
  EXPECT_TRUE(store_.GetModel(kTestModelName, /*is_consented=*/false).ok());
  EXPECT_TRUE(store_.GetModel(kTestModelName, /*is_consented=*/true).ok());
}

TEST_F(ModelStoreTest, PutModelFailureDoesNotWrite) {
  store_.SetModelConstructorForTestOnly(FailureModelConstructor);
  RegisterModelRequest request;
  EXPECT_FALSE(
      store_.PutModel(kTestModelName, request, model_construct_metrics_).ok());
  EXPECT_FALSE(store_.GetModel(kTestModelName, /*is_consented=*/false).ok());
  EXPECT_FALSE(store_.GetModel(kTestModelName, /*is_consented=*/true).ok());
}

TEST_F(ModelStoreTest, GetNonExistentModelReturnsNotFound) {
  absl::StatusOr<std::shared_ptr<MockModel>> model =
      store_.GetModel(kNonExistModelName);
  ASSERT_FALSE(model.ok());
  EXPECT_EQ(model.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(ModelStoreTest, ResetModelSuccess) {
  RegisterModelRequest request;
  ASSERT_TRUE(
      store_.PutModel(kTestModelName, request, model_construct_metrics_).ok());

  absl::StatusOr<std::shared_ptr<MockModel>> result1 =
      store_.GetModel(kTestModelName);
  ASSERT_TRUE(result1.ok());
  (*result1)->Increment();

  ASSERT_TRUE(store_.ResetModel(kTestModelName).ok());

  absl::StatusOr<std::shared_ptr<MockModel>> result2 =
      store_.GetModel(kTestModelName);
  ASSERT_TRUE(result2.ok());
  EXPECT_EQ((*result2)->GetCounter(), 0);
}

TEST_F(ModelStoreTest, ResetModelFailureDoesNotOverwrite) {
  RegisterModelRequest request;
  ASSERT_TRUE(
      store_.PutModel(kTestModelName, request, model_construct_metrics_).ok());

  absl::StatusOr<std::shared_ptr<MockModel>> result1 =
      store_.GetModel(kTestModelName);
  ASSERT_TRUE(result1.ok());
  (*result1)->Increment();

  store_.SetModelConstructorForTestOnly(FailureModelConstructor);
  ASSERT_FALSE(store_.ResetModel(kTestModelName).ok());

  absl::StatusOr<std::shared_ptr<MockModel>> result2 =
      store_.GetModel(kTestModelName);
  ASSERT_TRUE(result2.ok());
  EXPECT_EQ((*result2)->GetCounter(), 1);
}

TEST_F(ModelStoreTest, ResetNonExistentModelReturnsOkStatus) {
  absl::Status status = store_.ResetModel(kNonExistModelName);
  EXPECT_TRUE(status.ok());
}

TEST_F(ModelStoreTest, ListModels) {
  EXPECT_TRUE(store_.ListModels().empty());

  RegisterModelRequest request;
  EXPECT_TRUE(
      store_.PutModel(kTestModelName, request, model_construct_metrics_).ok());
  std::vector<std::string> models = store_.ListModels();
  EXPECT_EQ(models.size(), 1);
  EXPECT_EQ(models.front(), kTestModelName);
}

TEST_F(ModelStoreTest, DeleteModelsSuccess) {
  RegisterModelRequest request;
  EXPECT_TRUE(
      store_.PutModel(kTestModelName, request, model_construct_metrics_).ok());
  EXPECT_TRUE(store_.GetModel(kTestModelName, /*is_consented=*/false).ok());
  EXPECT_TRUE(store_.GetModel(kTestModelName, /*is_consented=*/true).ok());

  EXPECT_TRUE(store_.DeleteModel(kTestModelName).ok());

  EXPECT_FALSE(store_.GetModel(kTestModelName, /*is_consented=*/false).ok());
  EXPECT_FALSE(store_.GetModel(kTestModelName, /*is_consented=*/true).ok());
}

TEST_F(ModelStoreTest, DeleteNonExistentModelReturnsNotFound) {
  absl::Status result = store_.DeleteModel(kTestModelName);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.code(), absl::StatusCode::kNotFound);
}

absl::StatusOr<std::unique_ptr<MockModel>> MockModelConstructorWithInitCounter(
    const InferenceSidecarRuntimeConfig& config,
    const RegisterModelRequest& request,
    ModelConstructMetrics& construct_metrics) {
  int init_counter;
  auto it = request.model_files().find(kTestModelName);
  if (it == request.model_files().end() ||
      !absl::SimpleAtoi(it->second, &init_counter)) {
    return absl::FailedPreconditionError("Error");
  }
  return std::make_unique<MockModel>(init_counter);
}

class ModelStoreConcurrencyTest : public ::testing::Test {
 protected:
  ModelStoreConcurrencyTest()
      : store_(InferenceSidecarRuntimeConfig(),
               MockModelConstructorWithInitCounter) {}

  MockModelStore store_;
  ModelConstructMetrics model_construct_metrics_;
};

RegisterModelRequest BuildMockModelRegisterModelRequest(int init_counter) {
  RegisterModelRequest request;
  (*request.mutable_model_files())[kTestModelName] = absl::StrCat(init_counter);
  return request;
}

TEST_F(ModelStoreConcurrencyTest,
       ConcurrentPutModelEnsuresConsistentModelAcrossProdAndNonProd) {
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 2);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(this->store_
                      .PutModel(kTestModelName,
                                BuildMockModelRegisterModelRequest(1),
                                model_construct_metrics_)
                      .ok());
    }));
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(this->store_
                      .PutModel(kTestModelName,
                                BuildMockModelRegisterModelRequest(2),
                                model_construct_metrics_)
                      .ok());
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }

  absl::StatusOr<std::shared_ptr<MockModel>> prod_model =
      store_.GetModel(kTestModelName, /*is_consented=*/false);
  ASSERT_TRUE(prod_model.ok());
  int prod_model_counter = (*prod_model)->GetCounter();

  absl::StatusOr<std::shared_ptr<MockModel>> consented_model =
      store_.GetModel(kTestModelName, /*is_consented=*/true);
  ASSERT_TRUE(consented_model.ok());
  int consented_model_counter = (*consented_model)->GetCounter();

  EXPECT_EQ(prod_model_counter, consented_model_counter);
}

TEST_F(ModelStoreConcurrencyTest, ConcurrentPutAndDeleteModelSuccess) {
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 2);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(this->store_
                      .PutModel(kTestModelName,
                                BuildMockModelRegisterModelRequest(1),
                                model_construct_metrics_)
                      .ok());
    }));
    threads.push_back(
        std::thread([this]() { this->store_.DeleteModel(kTestModelName); }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(ModelStoreConcurrencyTest, ConcurrentGetAndDeleteModelSuccess) {
  for (int i = 0; i < kNumThreads; ++i) {
    EXPECT_TRUE(this->store_
                    .PutModel(absl::StrCat(kTestModelName, i),
                              BuildMockModelRegisterModelRequest(1),
                              model_construct_metrics_)
                    .ok());
  }

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 2);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this, i]() {
      auto get_result = this->store_.GetModel(absl::StrCat(kTestModelName, i));
      if (get_result.ok()) {
        (*get_result)->Increment();
        store_.IncrementModelInferenceCount(absl::StrCat(kTestModelName, i));
      }
    }));
    threads.push_back(std::thread([this, i]() {
      EXPECT_TRUE(
          this->store_.DeleteModel(absl::StrCat(kTestModelName, i)).ok());
    }));
  }
  for (auto& thread : threads) {
    thread.join();
  }

  for (int i = 0; i < kNumThreads; ++i) {
    EXPECT_FALSE(this->store_.GetModel(absl::StrCat(kTestModelName, i)).ok());
  }
}

TEST_F(ModelStoreConcurrencyTest, ConcurrentGetModelSuccess) {
  EXPECT_TRUE(store_
                  .PutModel(kTestModelName,
                            BuildMockModelRegisterModelRequest(1),
                            model_construct_metrics_)
                  .ok());

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 2);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this]() {
      absl::StatusOr<std::shared_ptr<MockModel>> prod_model =
          store_.GetModel(kTestModelName, /*is_consented=*/false);
      ASSERT_TRUE(prod_model.ok());
      EXPECT_EQ((*prod_model)->GetCounter(), 1);
    }));
    threads.push_back(std::thread([this]() {
      absl::StatusOr<std::shared_ptr<MockModel>> consented_model =
          store_.GetModel(kTestModelName, /*is_consented=*/true);
      ASSERT_TRUE(consented_model.ok());
      EXPECT_EQ((*consented_model)->GetCounter(), 1);
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(ModelStoreConcurrencyTest, ConcurrentResetModelSuccess) {
  EXPECT_TRUE(store_
                  .PutModel(kTestModelName,
                            BuildMockModelRegisterModelRequest(0),
                            model_construct_metrics_)
                  .ok());

  absl::StatusOr<std::shared_ptr<MockModel>> prod_model_result_1 =
      store_.GetModel(kTestModelName, /*is_consented=*/false);
  ASSERT_TRUE(prod_model_result_1.ok());
  (*prod_model_result_1)->Increment();

  absl::StatusOr<std::shared_ptr<MockModel>> consented_model_result_1 =
      store_.GetModel(kTestModelName, /*is_consented=*/true);
  ASSERT_TRUE(consented_model_result_1.ok());
  (*consented_model_result_1)->Increment();

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 2);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(
          this->store_.ResetModel(kTestModelName, /*is_consented=*/false).ok());
    }));
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(
          this->store_.ResetModel(kTestModelName, /*is_consented=*/true).ok());
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }

  absl::StatusOr<std::shared_ptr<MockModel>> prod_model_result_2 =
      store_.GetModel(kTestModelName, /*is_consented=*/false);
  ASSERT_TRUE(prod_model_result_2.ok());
  EXPECT_EQ((*prod_model_result_2)->GetCounter(), 0);

  absl::StatusOr<std::shared_ptr<MockModel>> consented_model_result_2 =
      store_.GetModel(kTestModelName, /*is_consented=*/true);
  ASSERT_TRUE(consented_model_result_2.ok());
  EXPECT_EQ((*consented_model_result_2)->GetCounter(), 0);
}

TEST_F(ModelStoreConcurrencyTest,
       GetModelReturnsModelDuringConcurrentResetModel) {
  EXPECT_TRUE(store_
                  .PutModel(kTestModelName,
                            BuildMockModelRegisterModelRequest(1),
                            model_construct_metrics_)
                  .ok());

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 4);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(
          this->store_.ResetModel(kTestModelName, /*is_consented=*/true).ok());
    }));
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(
          this->store_.ResetModel(kTestModelName, /*is_consented=*/false).ok());
    }));
    threads.push_back(std::thread([this]() {
      absl::StatusOr<std::shared_ptr<MockModel>> model =
          store_.GetModel(kTestModelName, /*is_consented=*/true);
      ASSERT_TRUE(model.ok());
      (*model)->Increment();
    }));
    threads.push_back(std::thread([this]() {
      absl::StatusOr<std::shared_ptr<MockModel>> model =
          store_.GetModel(kTestModelName, /*is_consented=*/false);
      ASSERT_TRUE(model.ok());
      (*model)->Increment();
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(ModelStoreConcurrencyTest,
       GetModelReturnsModelDuringConcurrentPutModel) {
  EXPECT_TRUE(store_
                  .PutModel(kTestModelName,
                            BuildMockModelRegisterModelRequest(1),
                            model_construct_metrics_)
                  .ok());

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 3);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(this->store_
                      .PutModel(kTestModelName,
                                BuildMockModelRegisterModelRequest(1),
                                model_construct_metrics_)
                      .ok());
    }));
    threads.push_back(std::thread([this]() {
      absl::StatusOr<std::shared_ptr<MockModel>> model =
          store_.GetModel(kTestModelName, /*is_consented=*/true);
      ASSERT_TRUE(model.ok());
      (*model)->Increment();
    }));
    threads.push_back(std::thread([this]() {
      absl::StatusOr<std::shared_ptr<MockModel>> model =
          store_.GetModel(kTestModelName, /*is_consented=*/false);
      ASSERT_TRUE(model.ok());
      (*model)->Increment();
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(ModelStoreConcurrencyTest,
       GetModelReturnsModelDuringConcurrentPutModelAndResetModel) {
  EXPECT_TRUE(store_
                  .PutModel(kTestModelName,
                            BuildMockModelRegisterModelRequest(1),
                            model_construct_metrics_)
                  .ok());

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 5);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(this->store_
                      .PutModel(kTestModelName,
                                BuildMockModelRegisterModelRequest(1),
                                model_construct_metrics_)
                      .ok());
    }));
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(
          this->store_.ResetModel(kTestModelName, /*is_consented=*/true).ok());
    }));
    threads.push_back(std::thread([this]() {
      EXPECT_TRUE(
          this->store_.ResetModel(kTestModelName, /*is_consented=*/false).ok());
    }));
    threads.push_back(std::thread([this]() {
      absl::StatusOr<std::shared_ptr<MockModel>> model =
          store_.GetModel(kTestModelName, /*is_consented=*/true);
      ASSERT_TRUE(model.ok());
      (*model)->Increment();
    }));
    threads.push_back(std::thread([this]() {
      absl::StatusOr<std::shared_ptr<MockModel>> model =
          store_.GetModel(kTestModelName, /*is_consented=*/false);
      ASSERT_TRUE(model.ok());
      (*model)->Increment();
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

class ModelStoreBackgroundModelResetTest : public ::testing::Test {
 protected:
  ModelStoreBackgroundModelResetTest()
      : store_(BuildAlwaysResetInferenceSidecarRuntimeConfig(),
               MockModelConstructorWithInitCounter) {}

  MockModelStore store_;
  ModelConstructMetrics model_construct_metrics_;

 private:
  InferenceSidecarRuntimeConfig
  BuildAlwaysResetInferenceSidecarRuntimeConfig() {
    InferenceSidecarRuntimeConfig config;
    config.set_model_reset_probability(1.0);
    return config;
  }
};

TEST_F(ModelStoreBackgroundModelResetTest, ResetSuccessWithStatefulModels) {
  for (int i = 0; i < kNumThreads; ++i) {
    EXPECT_TRUE(store_
                    .PutModel(absl::StrCat(kTestModelName, i),
                              BuildMockModelRegisterModelRequest(1),
                              model_construct_metrics_)
                    .ok());
  }

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this, i]() {
      absl::StatusOr<std::shared_ptr<MockModel>> prod_model = store_.GetModel(
          absl::StrCat(kTestModelName, i), /*is_consented=*/false);
      ASSERT_TRUE(prod_model.ok());
      EXPECT_EQ((*prod_model)->GetCounter(), 1);
      (*prod_model)->Increment();
      store_.IncrementModelInferenceCount(absl::StrCat(kTestModelName, i));
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }

  absl::SleepFor(absl::Seconds(1));
  for (int i = 0; i < kNumThreads; ++i) {
    absl::StatusOr<std::shared_ptr<MockModel>> prod_model = store_.GetModel(
        absl::StrCat(kTestModelName, i), /*is_consented=*/false);
    ASSERT_TRUE(prod_model.ok());
    EXPECT_EQ((*prod_model)->GetCounter(), 1);
  }
}

TEST_F(ModelStoreBackgroundModelResetTest, ResetSuccessWhileDeletingModels) {
  for (int i = 0; i < kNumThreads; ++i) {
    EXPECT_TRUE(store_
                    .PutModel(absl::StrCat(kTestModelName, i),
                              BuildMockModelRegisterModelRequest(1),
                              model_construct_metrics_)
                    .ok());
  }

  std::vector<std::thread> threads;
  threads.reserve(2 * kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::thread([this, i]() {
      absl::StatusOr<std::shared_ptr<MockModel>> prod_model = store_.GetModel(
          absl::StrCat(kTestModelName, i), /*is_consented=*/false);
      if (prod_model.ok()) {
        EXPECT_EQ((*prod_model)->GetCounter(), 1);
        (*prod_model)->Increment();
        store_.IncrementModelInferenceCount(absl::StrCat(kTestModelName, i));
      }
    }));
    // Delete all models except the (kNumThreads - 1)th model, which is reset.
    if (i < kNumThreads - 1) {
      threads.push_back(std::thread([this, i]() {
        EXPECT_TRUE(store_.DeleteModel(absl::StrCat(kTestModelName, i)).ok());
      }));
    }
  }

  for (auto& thread : threads) {
    thread.join();
  }

  absl::SleepFor(absl::Seconds(1));
  for (int i = 0; i < kNumThreads - 1; ++i) {
    EXPECT_FALSE(
        store_.GetModel(absl::StrCat(kTestModelName, 0), /*is_consented=*/false)
            .ok());
    EXPECT_FALSE(
        store_.GetModel(absl::StrCat(kTestModelName, 0), /*is_consented=*/true)
            .ok());
  }

  absl::StatusOr<std::shared_ptr<MockModel>> prod_model = store_.GetModel(
      absl::StrCat(kTestModelName, kNumThreads - 1), /*is_consented=*/false);
  ASSERT_TRUE(prod_model.ok());
  EXPECT_EQ((*prod_model)->GetCounter(), 1);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
