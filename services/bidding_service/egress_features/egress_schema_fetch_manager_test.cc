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

#include "services/bidding_service/egress_features/egress_schema_fetch_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/bidding_service/constants.h"
#include "services/bidding_service/egress_schema_fetch_config.pb.h"
#include "services/common/data_fetch/version_util.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/public/cpio/mock/blob_storage_client/mock_blob_storage_client.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::cmrt::sdk::blob_storage_service::v1::BlobMetadata;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::cpio::MockBlobStorageClient;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Return;

constexpr char kSchema[] = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": []
    }
  )JSON";

constexpr absl::string_view kTestBucketLimited = "limited";
constexpr absl::string_view kTestBucketUnlimited = "unlimited";

struct FetchConfig {
  // json string representation of the config
  std::string json;
  // protobuf object representation of the config
  bidding_service::EgressSchemaFetchConfig pb;
};

class EgressSchemaFetchManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    executor_ = std::make_unique<MockExecutor>();
    http_fetcher_ = std::make_unique<MockHttpFetcherAsync>();
    blob_storage_client_ = std::make_unique<MockBlobStorageClient>();

    EXPECT_CALL(*executor_, RunAfter)
        .WillRepeatedly(
            [](absl::Duration duration, absl::AnyInvocable<void()> closure) {
              return server_common::TaskId{};
            });
  }

  std::unique_ptr<MockExecutor> executor_;
  std::unique_ptr<MockHttpFetcherAsync> http_fetcher_;
  std::unique_ptr<MockBlobStorageClient> blob_storage_client_;
};

bidding_service::EgressSchemaFetchConfig CreateEgressSchemaFetchConfigProto(
    blob_fetch::FetchMode fetch_mode) {
  bidding_service::EgressSchemaFetchConfig proto;
  proto.set_fetch_mode(fetch_mode);
  proto.set_egress_schema_url("https://egress-schema-url.test");
  proto.set_temporary_unlimited_egress_schema_url(
      "https://temporary-unlimited-egress-schema-url.test");
  proto.set_egress_schema_bucket(kTestBucketLimited);
  proto.set_egress_default_schema_in_bucket("limited");
  proto.set_temporary_unlimited_egress_schema_bucket(kTestBucketUnlimited);
  proto.set_temporary_unlimited_egress_default_schema_in_bucket("unlimited");
  proto.set_url_fetch_period_ms(1000000);
  proto.set_url_fetch_timeout_ms(500);
  return proto;
}

std::string CreateEgressSchemaFetchConfigJson(
    const bidding_service::EgressSchemaFetchConfig& proto) {
  std::string json;
  absl::Status status =
      google::protobuf::util::MessageToJsonString(proto, &json);
  EXPECT_TRUE(status.ok()) << status;
  return json;
}

FetchConfig CreateEgressSchemaFetchConfig(blob_fetch::FetchMode fetch_mode) {
  bidding_service::EgressSchemaFetchConfig pb =
      CreateEgressSchemaFetchConfigProto(fetch_mode);
  std::string json = CreateEgressSchemaFetchConfigJson(pb);
  return {.json = std::move(json), .pb = std::move(pb)};
}

TEST_F(EgressSchemaFetchManagerTest,
       InitSucceedsWithProtectedAppSignalsDisabled) {
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*http_fetcher_, FetchUrls).Times(0);
  auto fetch_config =
      CreateEgressSchemaFetchConfig(blob_fetch::FETCH_MODE_BUCKET);
  EgressSchemaFetchManager manager({
      .enable_protected_app_signals = false,
      .enable_temporary_unlimited_egress = true,
      .limited_egress_bits = 1,
      .fetch_config = fetch_config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = std::move(blob_storage_client_),
      .temporary_unlimited_egress_cddl_cache = nullptr,
      .egress_cddl_cache = nullptr,
  });
  BiddingServiceRuntimeConfig runtime_config;
  auto result = manager.Init(runtime_config);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->egress_schema_cache, nullptr);
  EXPECT_EQ(result->unlimited_egress_schema_cache, nullptr);
}

TEST_F(EgressSchemaFetchManagerTest,
       InitSucceedsWithProtectedAppSignalsEnabledAndLimitedEgressEnabled) {
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);

  auto fetch_config = CreateEgressSchemaFetchConfig(blob_fetch::FETCH_MODE_URL);

  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&fetch_config](const std::vector<HTTPRequest>& requests,
                                absl::Duration timeout,
                                OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 1);
        EXPECT_EQ(requests[0].url, fetch_config.pb.egress_schema_url());
        std::move(done_callback)({kSchema});
      });

  auto mock_cddl = std::make_unique<CddlSpecCacheMock>("mock");
  EXPECT_CALL(*mock_cddl, Init).Times(1).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*mock_cddl, Get).WillOnce([](absl::string_view version) {
    return "schema = {}";
  });

  EgressSchemaFetchManager manager({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = false,
      .limited_egress_bits = 1,
      .fetch_config = fetch_config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = std::move(blob_storage_client_),
      .temporary_unlimited_egress_cddl_cache = nullptr,
      .egress_cddl_cache = std::move(mock_cddl),
  });
  BiddingServiceRuntimeConfig runtime_config;
  auto result = manager.Init(runtime_config);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result->egress_schema_cache, nullptr);
  EXPECT_EQ(result->unlimited_egress_schema_cache, nullptr);
}

TEST_F(EgressSchemaFetchManagerTest, InitSucceedsWithAllEgressEnabled) {
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);

  auto config = CreateEgressSchemaFetchConfig(blob_fetch::FETCH_MODE_URL);

  auto unlimited_cddl = std::make_unique<CddlSpecCacheMock>("unlimited");
  EXPECT_CALL(*unlimited_cddl, Init)
      .Times(1)
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*unlimited_cddl, Get).WillOnce([](absl::string_view version) {
    return "schema = {}";
  });

  auto limited_cddl = std::make_unique<CddlSpecCacheMock>("limited");
  EXPECT_CALL(*limited_cddl, Init).Times(1).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*limited_cddl, Get).WillOnce([](absl::string_view version) {
    return "schema = {}";
  });

  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&config = config.pb](const std::vector<HTTPRequest>& requests,
                                      absl::Duration timeout,
                                      OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 1);
        EXPECT_EQ(requests[0].url,
                  config.temporary_unlimited_egress_schema_url());
        std::move(done_callback)({kSchema});
      })
      .WillOnce([&config = config.pb](const std::vector<HTTPRequest>& requests,
                                      absl::Duration timeout,
                                      OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 1);
        EXPECT_EQ(requests[0].url, config.egress_schema_url());
        std::move(done_callback)({kSchema});
      });

  EgressSchemaFetchManager manager({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = true,
      .limited_egress_bits = 1,
      .fetch_config = config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = std::move(blob_storage_client_),
      .temporary_unlimited_egress_cddl_cache = std::move(unlimited_cddl),
      .egress_cddl_cache = std::move(limited_cddl),
  });
  BiddingServiceRuntimeConfig runtime_config;
  auto result = manager.Init(runtime_config);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result->egress_schema_cache, nullptr);
  EXPECT_NE(result->unlimited_egress_schema_cache, nullptr);
}

TEST_F(EgressSchemaFetchManagerTest,
       InitSucceedsAllEgressTypesEnabledForBucketFetch) {
  EXPECT_CALL(*blob_storage_client_, Init()).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run()).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*http_fetcher_, FetchUrls).Times(0);

  auto config = CreateEgressSchemaFetchConfig(blob_fetch::FETCH_MODE_BUCKET);

  auto unlimited_cddl = std::make_unique<CddlSpecCacheMock>("unlimited");
  EXPECT_CALL(*unlimited_cddl, Init)
      .Times(1)
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*unlimited_cddl, Get).WillOnce([](absl::string_view version) {
    return "schema = {}";
  });

  auto limited_cddl = std::make_unique<CddlSpecCacheMock>("limited");
  EXPECT_CALL(*limited_cddl, Init).Times(1).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*limited_cddl, Get).WillOnce([](absl::string_view version) {
    return "schema = {}";
  });

  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&config = config.pb](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  async_context) {
            EXPECT_EQ(async_context.request->blob_metadata().bucket_name(),
                      config.temporary_unlimited_egress_schema_bucket());
            async_context.response =
                std::make_shared<ListBlobsMetadataResponse>();
            BlobMetadata blob1;
            blob1.set_bucket_name(
                config.temporary_unlimited_egress_schema_bucket());
            blob1.set_blob_name("test");
            async_context.response->mutable_blob_metadatas()->Add(
                std::move(blob1));
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return absl::OkStatus();
          })
      .WillOnce(
          [&config = config.pb](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  async_context) {
            EXPECT_EQ(async_context.request->blob_metadata().bucket_name(),
                      config.egress_schema_bucket());
            async_context.response =
                std::make_shared<ListBlobsMetadataResponse>();
            BlobMetadata blob1;
            blob1.set_bucket_name(config.egress_schema_bucket());
            blob1.set_blob_name("test");
            async_context.response->mutable_blob_metadatas()->Add(
                std::move(blob1));
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return absl::OkStatus();
          });

  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .Times(2)
      .WillRepeatedly(
          [](AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(kSchema);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });

  EgressSchemaFetchManager manager({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = true,
      .limited_egress_bits = 5,
      .fetch_config = config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = std::move(blob_storage_client_),
      .temporary_unlimited_egress_cddl_cache = std::move(unlimited_cddl),
      .egress_cddl_cache = std::move(limited_cddl),
  });
  BiddingServiceRuntimeConfig runtime_config;
  auto result = manager.Init(runtime_config);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_NE(result->egress_schema_cache, nullptr);
  EXPECT_NE(result->unlimited_egress_schema_cache, nullptr);
}

TEST_F(EgressSchemaFetchManagerTest,
       ConfigureRuntimeDefaultsDemandsAtLeastOneSchema) {
  BiddingServiceRuntimeConfig runtime_config;
  auto fetch_config = CreateEgressSchemaFetchConfig(blob_fetch::FETCH_MODE_URL);
  const std::string bad_cache_msg = "Bad cache.";
  auto bad_limited_cache = std::make_unique<CddlSpecCacheMock>("limited");
  EXPECT_CALL(*bad_limited_cache, Init)
      .WillOnce(Return(absl::UnavailableError(bad_cache_msg)));

  EgressSchemaFetchManager manager_limited_egress_only({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = false,
      .limited_egress_bits = 5,
      .fetch_config = fetch_config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = nullptr,
      .temporary_unlimited_egress_cddl_cache = nullptr,
      .egress_cddl_cache = std::move(bad_limited_cache),
  });
  auto limited_egress_result = manager_limited_egress_only.Init(runtime_config);
  EXPECT_THAT(limited_egress_result.status().message(),
              testing::HasSubstr(bad_cache_msg));

  auto bad_unlimited_cache = std::make_unique<CddlSpecCacheMock>("unlimited");
  EXPECT_CALL(*bad_unlimited_cache, Init)
      .WillOnce(Return(absl::UnavailableError(bad_cache_msg)));

  EgressSchemaFetchManager manager_unlimited_egress_only({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = true,
      .limited_egress_bits = 0,
      .fetch_config = fetch_config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = nullptr,
      .temporary_unlimited_egress_cddl_cache = std::move(bad_unlimited_cache),
      .egress_cddl_cache = nullptr,
  });
  auto unlimited_egress_result =
      manager_unlimited_egress_only.Init(runtime_config);
  EXPECT_THAT(unlimited_egress_result.status().message(),
              testing::HasSubstr(bad_cache_msg));

  EgressSchemaFetchManager manager_no_egress({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = false,
      .limited_egress_bits = 0,
      .fetch_config = fetch_config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = nullptr,
      .temporary_unlimited_egress_cddl_cache = nullptr,
      .egress_cddl_cache = nullptr,
  });
  auto no_egress_result = manager_no_egress.Init(runtime_config);
  EXPECT_THAT(no_egress_result.status().message(),
              testing::HasSubstr(kMustUseTemporaryOrLimitedEgress));
}

TEST_F(EgressSchemaFetchManagerTest,
       ConfigureRuntimeDefaultsUsesDefaultSchemaId) {
  auto fetch_config = CreateEgressSchemaFetchConfig(blob_fetch::FETCH_MODE_URL);
  auto bad_cache = std::make_unique<CddlSpecCacheMock>("unlimited");
  const std::string bad_cache_msg = "Cache init failed.";
  EXPECT_CALL(*bad_cache, Init)
      .WillOnce(Return(absl::UnavailableError(bad_cache_msg)));
  EgressSchemaFetchManager manager({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = true,
      .limited_egress_bits = 5,
      .fetch_config = fetch_config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = std::move(blob_storage_client_),
      .temporary_unlimited_egress_cddl_cache = std::move(bad_cache),
      .egress_cddl_cache = nullptr,
  });
  BiddingServiceRuntimeConfig runtime_config;
  auto result = manager.Init(runtime_config);
  EXPECT_THAT(result.status().message(), testing::HasSubstr(bad_cache_msg));
  EXPECT_FALSE(runtime_config.use_per_request_schema_versioning);
  EXPECT_EQ(runtime_config.default_egress_schema_version,
            kDefaultEgressSchemaId);
  EXPECT_EQ(runtime_config.default_unlimited_egress_schema_version,
            kDefaultEgressSchemaId);
}

TEST_F(EgressSchemaFetchManagerTest, ConfigureRuntimeDefaultsUsesBucketInfo) {
  const std::string init_error = "Skipping bucket fetch.";
  EXPECT_CALL(*blob_storage_client_, Init())
      .WillOnce(Return(absl::UnavailableError(init_error)));

  auto fetch_config =
      CreateEgressSchemaFetchConfig(blob_fetch::FETCH_MODE_BUCKET);
  EgressSchemaFetchManager manager({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = true,
      .limited_egress_bits = 5,
      .fetch_config = fetch_config.json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = std::move(blob_storage_client_),
      .temporary_unlimited_egress_cddl_cache = nullptr,
      .egress_cddl_cache = nullptr,
  });
  BiddingServiceRuntimeConfig runtime_config;
  auto result = manager.Init(runtime_config);
  EXPECT_THAT(result.status().message(), testing::HasSubstr(init_error));
  EXPECT_TRUE(runtime_config.use_per_request_schema_versioning);
  EXPECT_EQ(
      runtime_config.default_egress_schema_version,
      *GetBucketBlobVersion(kTestBucketLimited,
                            fetch_config.pb.egress_default_schema_in_bucket()));
  EXPECT_EQ(runtime_config.default_unlimited_egress_schema_version,
            *GetBucketBlobVersion(
                kTestBucketUnlimited,
                fetch_config.pb
                    .temporary_unlimited_egress_default_schema_in_bucket()));
}

TEST_F(EgressSchemaFetchManagerTest,
       ConfigureRuntimeDefaultsErrorsOnBadBucketSpecs) {
  BiddingServiceRuntimeConfig runtime_config;
  auto bad_unlimited_schema_fetch_config =
      CreateEgressSchemaFetchConfigProto(blob_fetch::FETCH_MODE_BUCKET);
  bad_unlimited_schema_fetch_config
      .set_temporary_unlimited_egress_default_schema_in_bucket("");
  std::string unlimited_fetch_config_json =
      CreateEgressSchemaFetchConfigJson(bad_unlimited_schema_fetch_config);
  EgressSchemaFetchManager manager_unlimited_egress({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = true,
      .limited_egress_bits = 5,
      .fetch_config = unlimited_fetch_config_json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = nullptr,
      .temporary_unlimited_egress_cddl_cache = nullptr,
      .egress_cddl_cache = nullptr,
  });
  auto result = manager_unlimited_egress.Init(runtime_config);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr(kNoUnlimitedSchemaVersion));

  auto bad_limited_schema_fetch_config =
      CreateEgressSchemaFetchConfigProto(blob_fetch::FETCH_MODE_BUCKET);
  bad_limited_schema_fetch_config.set_egress_default_schema_in_bucket("");
  std::string limited_fetch_config_json =
      CreateEgressSchemaFetchConfigJson(bad_limited_schema_fetch_config);
  EgressSchemaFetchManager manager_limited_egress({
      .enable_protected_app_signals = true,
      .enable_temporary_unlimited_egress = true,
      .limited_egress_bits = 5,
      .fetch_config = limited_fetch_config_json,
      .executor = executor_.get(),
      .http_fetcher_async = http_fetcher_.get(),
      .blob_storage_client = nullptr,
      .temporary_unlimited_egress_cddl_cache = nullptr,
      .egress_cddl_cache = nullptr,
  });
  result = manager_limited_egress.Init(runtime_config);
  EXPECT_THAT(result.status().message(),
              testing::HasSubstr(kNoLimitedSchemaVersion));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
