// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/buyer_code_fetch_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "services/bidding_service/bidding_code_fetch_config.pb.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/constants.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/code_fetch/periodic_bucket_fetcher.h"
#include "services/common/code_fetch/periodic_code_fetcher.h"
#include "services/common/util/file_util.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

#include "buyer_code_fetch_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr int kJsBlobIndex = 0;
constexpr int kWasmBlobIndex = 1;
constexpr int kMinNumCodeBlobs = 1;
constexpr int kMaxNumCodeBlobs = 2;
constexpr char kUnusedWasmBlob[] = "";

using ::google::scp::core::errors::GetErrorMessage;

}  // namespace

BuyerCodeFetchManager::~BuyerCodeFetchManager() {
  if (absl::Status shutdown = End(); !shutdown.ok()) {
    PS_LOG(ERROR) << "BuyerCodeFetchManager shutdown failed. " << shutdown;
  }
}

absl::Status BuyerCodeFetchManager::Init() {
  switch (udf_config_.fetch_mode()) {
    case blob_fetch::FETCH_MODE_LOCAL:
      return InitializeLocalCodeFetch();
    case blob_fetch::FETCH_MODE_BUCKET:
      return InitializeBucketCodeFetch();
    case blob_fetch::FETCH_MODE_URL:
      return InitializeUrlCodeFetch();
    default:
      return absl::InvalidArgumentError(kFetchModeInvalid);
  }
}

absl::Status BuyerCodeFetchManager::End() {
  if (enable_protected_audience_) {
    if (pa_udf_fetcher_ != nullptr) {
      pa_udf_fetcher_->End();
    }
  }
  if (enable_protected_app_signals_) {
    if (pas_bidding_udf_fetcher_ != nullptr) {
      pas_bidding_udf_fetcher_->End();
    }
    if (pas_ads_retrieval_udf_fetcher_ != nullptr) {
      pas_ads_retrieval_udf_fetcher_->End();
    }
  }
  return absl::OkStatus();
}

// PA only; PAS not supported.
absl::Status BuyerCodeFetchManager::InitializeLocalCodeFetch() {
  if (udf_config_.bidding_js_path().empty()) {
    return absl::UnavailableError(kLocalFetchNeedsPath);
  }
  PS_ASSIGN_OR_RETURN(auto adtech_code_blob,
                      GetFileContent(udf_config_.bidding_js_path(),
                                     /*log_on_error=*/true));
  adtech_code_blob = GetBuyerWrappedCode(adtech_code_blob, "");
  return dispatcher_.LoadSync(kProtectedAudienceGenerateBidBlobVersion,
                              adtech_code_blob);
}

absl::Status BuyerCodeFetchManager::InitializeBucketCodeFetch() {
  if (enable_protected_audience_ || enable_protected_app_signals_) {
    PS_RETURN_IF_ERROR(InitBucketClient()) << kBlobStorageClientInitFailed;
  }
  if (enable_protected_audience_) {
    PS_RETURN_IF_ERROR(InitializeBucketCodeFetchForPA());
  }
  if (enable_protected_app_signals_) {
    PS_RETURN_IF_ERROR(InitializeBucketCodeFetchForPAS());
  }
  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManager::InitializeBucketCodeFetchForPA() {
  auto wrap_code = [](const std::vector<std::string>& adtech_code_blobs) {
    return GetBuyerWrappedCode(adtech_code_blobs[kJsBlobIndex], kUnusedWasmBlob,
                               AuctionType::kProtectedAudience);
  };

  PS_ASSIGN_OR_RETURN(
      pa_udf_fetcher_,
      StartBucketFetch(
          udf_config_.protected_auction_bidding_js_bucket(),
          udf_config_.protected_auction_bidding_js_bucket_default_blob(),
          kProtectedAuctionJsId,
          absl::Milliseconds(udf_config_.url_fetch_period_ms()),
          std::move(wrap_code)));

  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManager::InitializeBucketCodeFetchForPAS() {
  auto wrap_bidding_code =
      [](const std::vector<std::string>& ad_tech_code_blobs) {
        return GetBuyerWrappedCode(
            ad_tech_code_blobs[kJsBlobIndex], kUnusedWasmBlob,
            AuctionType::kProtectedAppSignals,
            /*auction_specific_setup=*/kEncodedProtectedAppSignalsHandler);
      };

  auto wrap_ads_retrieval_code =
      [](const std::vector<std::string>& ad_tech_code_blobs) {
        return GetProtectedAppSignalsGenericBuyerWrappedCode(
            ad_tech_code_blobs[kJsBlobIndex], kUnusedWasmBlob,
            kPrepareDataForAdRetrievalHandler, kPrepareDataForAdRetrievalArgs);
      };

  absl::Duration url_fetch_period_ms =
      absl::Milliseconds(udf_config_.url_fetch_period_ms());

  PS_ASSIGN_OR_RETURN(
      pas_bidding_udf_fetcher_,
      StartBucketFetch(
          udf_config_.protected_app_signals_bidding_js_bucket(),
          udf_config_.protected_app_signals_bidding_js_bucket_default_blob(),
          kProtectedAppSignalsJsId, url_fetch_period_ms,
          std::move(wrap_bidding_code)));

  PS_ASSIGN_OR_RETURN(
      pas_ads_retrieval_udf_fetcher_,
      StartBucketFetch(udf_config_.ads_retrieval_js_bucket(),
                       udf_config_.ads_retrieval_bucket_default_blob(),
                       kAdsRetrievalJsId, url_fetch_period_ms,
                       std::move(wrap_ads_retrieval_code)));

  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManager::InitializeUrlCodeFetch() {
  if (enable_protected_audience_) {
    PS_RETURN_IF_ERROR(InitializeUrlCodeFetchForPA());
  }
  if (enable_protected_app_signals_) {
    PS_RETURN_IF_ERROR(InitializeUrlCodeFetchForPAS());
  }

  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManager::InitializeUrlCodeFetchForPA() {
  auto wrap_code = [](const std::vector<std::string>& adtech_code_blobs) {
    return GetBuyerWrappedCode(adtech_code_blobs[kJsBlobIndex],
                               adtech_code_blobs.size() == kMaxNumCodeBlobs
                                   ? adtech_code_blobs[kWasmBlobIndex]
                                   : kUnusedWasmBlob,
                               AuctionType::kProtectedAudience);
  };

  PS_ASSIGN_OR_RETURN(
      pa_udf_fetcher_,
      StartUrlFetch(udf_config_.bidding_js_url(),
                    udf_config_.bidding_wasm_helper_url(),
                    kProtectedAudienceGenerateBidBlobVersion, "bidding_js_url",
                    absl::Milliseconds(udf_config_.url_fetch_period_ms()),
                    absl::Milliseconds(udf_config_.url_fetch_timeout_ms()),
                    std::move(wrap_code)));
  return absl::OkStatus();
}

absl::Status BuyerCodeFetchManager::InitializeUrlCodeFetchForPAS() {
  auto wrap_bidding_code =
      [](const std::vector<std::string>& ad_tech_code_blobs) {
        DCHECK_GE(ad_tech_code_blobs.size(), kMinNumCodeBlobs);
        DCHECK_LE(ad_tech_code_blobs.size(), kMaxNumCodeBlobs);
        return GetBuyerWrappedCode(
            ad_tech_code_blobs[kJsBlobIndex],
            ad_tech_code_blobs.size() == kMaxNumCodeBlobs
                ? ad_tech_code_blobs[kWasmBlobIndex]
                : kUnusedWasmBlob,
            AuctionType::kProtectedAppSignals,
            /*auction_specific_setup=*/kEncodedProtectedAppSignalsHandler);
      };

  auto wrap_ads_retrieval_code =
      [](const std::vector<std::string>& ad_tech_code_blobs) {
        DCHECK_GE(ad_tech_code_blobs.size(), kMinNumCodeBlobs);
        DCHECK_LE(ad_tech_code_blobs.size(), kMaxNumCodeBlobs);
        return GetProtectedAppSignalsGenericBuyerWrappedCode(
            ad_tech_code_blobs[kJsBlobIndex],
            ad_tech_code_blobs.size() == kMaxNumCodeBlobs
                ? ad_tech_code_blobs[kWasmBlobIndex]
                : kUnusedWasmBlob,
            kPrepareDataForAdRetrievalHandler, kPrepareDataForAdRetrievalArgs);
      };

  absl::Duration url_fetch_period_ms =
      absl::Milliseconds(udf_config_.url_fetch_period_ms());
  absl::Duration url_fetch_timeout_ms =
      absl::Milliseconds(udf_config_.url_fetch_timeout_ms());

  PS_ASSIGN_OR_RETURN(
      pas_bidding_udf_fetcher_,
      StartUrlFetch(udf_config_.protected_app_signals_bidding_js_url(),
                    udf_config_.protected_app_signals_bidding_wasm_helper_url(),
                    kProtectedAppSignalsGenerateBidBlobVersion,
                    "protected_app_signals_bidding_js_url", url_fetch_period_ms,
                    url_fetch_timeout_ms, std::move(wrap_bidding_code)));

  PS_ASSIGN_OR_RETURN(
      pas_ads_retrieval_udf_fetcher_,
      StartUrlFetch(
          udf_config_.prepare_data_for_ads_retrieval_js_url(),
          udf_config_.prepare_data_for_ads_retrieval_wasm_helper_url(),
          kPrepareDataForAdRetrievalBlobVersion,
          "prepare_data_for_ads_retrieval_js_url", url_fetch_period_ms,
          url_fetch_timeout_ms, std::move(wrap_ads_retrieval_code)));

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<CodeFetcherInterface>>
BuyerCodeFetchManager::StartUrlFetch(
    const std::string& js_url, const std::string& wasm_helper_url,
    const std::string& roma_version, absl::string_view script_logging_name,
    absl::Duration url_fetch_period_ms, absl::Duration url_fetch_timeout_ms,
    absl::AnyInvocable<std::string(const std::vector<std::string>&)>
        wrap_code) {
  if (js_url.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("JS URL for ", script_logging_name, " is missing"));
  }

  std::vector<std::string> endpoints = {js_url};
  if (!wasm_helper_url.empty()) {
    endpoints.emplace_back(wasm_helper_url);
  }
  auto code_fetcher = std::make_unique<PeriodicCodeFetcher>(
      std::move(endpoints), url_fetch_period_ms, &http_fetcher_, &dispatcher_,
      &executor_, url_fetch_timeout_ms, std::move(wrap_code), roma_version);

  PS_RETURN_IF_ERROR(code_fetcher->Start())
      << absl::StrCat("Failed url fetcher startup for  ", script_logging_name);
  return code_fetcher;
}

absl::StatusOr<std::unique_ptr<CodeFetcherInterface>>
BuyerCodeFetchManager::StartBucketFetch(
    const std::string& bucket_name, const std::string& default_version,
    absl::string_view script_logging_name, absl::Duration url_fetch_period_ms,
    absl::AnyInvocable<std::string(const std::vector<std::string>&)>
        wrap_code) {
  if (bucket_name.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat(kEmptyBucketName, script_logging_name));
  } else if (default_version.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat(kEmptyBucketDefault, script_logging_name));
  }

  auto bucket_fetcher = std::make_unique<PeriodicBucketFetcher>(
      bucket_name, url_fetch_period_ms, &dispatcher_, &executor_,
      std::move(wrap_code), blob_storage_client_.get());
  PS_RETURN_IF_ERROR(bucket_fetcher->Start())
      << absl::StrCat("Failed bucket fetch startup for ", script_logging_name,
                      " ", bucket_name);
  return bucket_fetcher;
}

absl::Status BuyerCodeFetchManager::InitBucketClient() {
  PS_RETURN_IF_ERROR(blob_storage_client_->Init()).SetPrepend()
      << "Failed to init BlobStorageClient: ";
  PS_RETURN_IF_ERROR(blob_storage_client_->Run()).SetPrepend()
      << "Failed to run BlobStorageClient: ";
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
