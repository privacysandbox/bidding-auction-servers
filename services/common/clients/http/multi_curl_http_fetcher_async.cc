//  Copyright 2022 Google LLC
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

#include "services/common/clients/http/multi_curl_http_fetcher_async.h"

#include <stdlib.h>

#include <algorithm>
#include <random>
#include <sstream>
#include <string>
#include <utility>

#include <curl/curl.h>

#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "event2/thread.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/file_util.h"

namespace privacy_sandbox::bidding_auction_servers {
using ::grpc_event_engine::experimental::EventEngine;

MultiCurlHttpFetcherAsync::MultiCurlHttpFetcherAsync(
    server_common::Executor* executor,
    const MultiCurlHttpFetcherAsyncOptions& options)
    : executor_(executor),
      keepalive_idle_sec_(options.keepalive_idle_sec),
      keepalive_interval_sec_(options.keepalive_interval_sec),
      skip_tls_verification_(
          absl::GetFlag(FLAGS_https_fetch_skips_tls_verification)
              .value_or(false)),
      ca_cert_(options.ca_cert),
      num_curl_workers_(options.num_curl_workers) {
  DCHECK_GT(num_curl_workers_, 0);
  auto ca_cert_blob = GetFileContent(ca_cert_, /*log_on_error=*/true);
  CHECK_OK(ca_cert_blob);
  ca_cert_blob_ = *std::move(ca_cert_blob);

  // Setup curl workers and their work queues.
  curl_request_workers_.reserve(num_curl_workers_);
  for (int i = 0; i < options.num_curl_workers; ++i) {
    auto request_queue = std::make_unique<CurlRequestQueue>(
        executor_, options.curl_queue_length, options.curl_max_wait_time_ms);
    curl_request_workers_.push_back(std::make_unique<CurlRequestWorker>(
        executor_, *request_queue, options.curlmopt_maxconnects,
        options.curlmopt_max_total_connections,
        options.curlmopt_max_host_connections));
    request_queues_.emplace_back(std::move(request_queue));
  }
}

// The function declaration of WriteCallback is specified by libcurl.
// Please do not modify the parameter types or ordering, although you
// may modify the function name and body.
// libcurl documentation: https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
//
// data: A pointer to the data that was delivered over the wire.
// size: (legacy) size is always 1. Represents 1 byte.
// number_elements: the number of elements (each of size 1 byte) to write
// output: a libcurl-client-provided pointer of where to save the data
// return: number of bytes actually written to output
static size_t WriteCallback(char* data, size_t size, size_t number_elements,
                            std::string* output) {
  output->append(reinterpret_cast<char*>(data), size * number_elements);
  return size * number_elements;
}

// The function declaration of ReadCallback is specified by libcurl.
// Please do not modify the parameter types or ordering, although you
// may modify the function name and body.
// libcurl documentation: https://curl.se/libcurl/c/CURLOPT_READFUNCTION.html
static size_t ReadCallback(char* data, size_t size, size_t num_items,
                           void* userdata) {
  auto* to_upload = static_cast<DataToUpload*>(userdata);
  if (to_upload->offset >= to_upload->data.size()) {
    // No more data to upload.
    return 0;
  }
  size_t num_bytes_to_upload =
      std::min(to_upload->data.size() - to_upload->offset, num_items * size);
  memcpy(data, to_upload->data.c_str() + to_upload->offset,
         num_bytes_to_upload);
  PS_VLOG(8) << "PUTing data (offset: " << to_upload->offset
             << ", chunk size: " << num_bytes_to_upload
             << "): " << to_upload->data;
  to_upload->offset += num_bytes_to_upload;
  return num_bytes_to_upload;
}

void MultiCurlHttpFetcherAsync::FetchUrls(
    const std::vector<HTTPRequest>& requests, absl::Duration timeout,
    OnDoneFetchUrls done_callback) {
  FetchUrlsWithMetadata(
      requests, timeout,
      [done_callback = std::move(done_callback)](
          std::vector<absl::StatusOr<HTTPResponse>> response_vector) mutable {
        std::vector<absl::StatusOr<std::string>> results;
        results.reserve(response_vector.size());
        for (auto& response : response_vector) {
          if (response.ok()) {
            results.emplace_back(std::move(response)->body);
          } else {
            results.emplace_back(std::move(response).status());
          }
        }
        std::move(done_callback)(std::move(results));
      });
}

void MultiCurlHttpFetcherAsync::FetchUrlsWithMetadata(
    const std::vector<HTTPRequest>& requests, absl::Duration timeout,
    OnDoneFetchUrlsWithMetadata done_callback) {
  // The FetchUrl lambdas are the owners of the underlying FetchUrlsLifetime and
  // this shared_ptr will be destructed once the last FetchUrl lambda finishes.
  // Using a shared_ptr here allows us to avoid making MultiCurlHttpFetcherAsync
  // the owner of the FetchUrlsLifetime, which would complicate cleanup on
  // MultiCurlHttpFetcherAsync destruction during a pending FetchUrls call.
  auto shared_lifetime = std::make_shared<
      MultiCurlHttpFetcherAsync::FetchUrlsWithMetadataLifetime>();
  shared_lifetime->pending_results = requests.size();
  shared_lifetime->all_done_callback = std::move(done_callback);
  shared_lifetime->results =
      std::vector<absl::StatusOr<HTTPResponse>>(requests.size());
  if (requests.empty()) {
    // Execute callback immediately if there are no requests.
    std::move(shared_lifetime->all_done_callback)(
        std::move(shared_lifetime->results));
    return;
  }
  for (int i = 0; i < requests.size(); i++) {
    FetchUrlWithMetadata(
        requests.at(i), absl::ToInt64Milliseconds(timeout),
        [i, shared_lifetime](absl::StatusOr<HTTPResponse> result) {
          absl::MutexLock lock_results(&shared_lifetime->results_mu);
          shared_lifetime->results[i] = std::move(result);
          if (--shared_lifetime->pending_results == 0) {
            std::move(shared_lifetime->all_done_callback)(
                std::move(shared_lifetime->results));
          }
        });
  }
}

std::unique_ptr<CurlRequestData> MultiCurlHttpFetcherAsync::CreateCurlRequest(
    const HTTPRequest& request, int timeout_ms, int64_t keepalive_idle_sec,
    int64_t keepalive_interval_sec, OnDoneFetchUrlWithMetadata done_callback) {
  auto curl_request_data = std::make_unique<CurlRequestData>(
      request.headers, std::move(done_callback), request.include_headers,
      request.redirect_config.get_redirect_url);
  CURL* req_handle = curl_request_data->req_handle;
  curl_easy_setopt(req_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(req_handle, CURLOPT_URL, request.url.begin());
  curl_easy_setopt(req_handle, CURLOPT_WRITEDATA,
                   &curl_request_data->response_with_metadata.body);
  curl_easy_setopt(req_handle, CURLOPT_FOLLOWLOCATION, 1);
  if (request.redirect_config.strict_http) {
    curl_easy_setopt(req_handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
  }

  curl_easy_setopt(req_handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  // Enable TCP keep-alive to keep connection warm.
  curl_easy_setopt(req_handle, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(req_handle, CURLOPT_TCP_KEEPIDLE,
                   static_cast<long>(keepalive_idle_sec));
  curl_easy_setopt(req_handle, CURLOPT_TCP_KEEPINTVL,
                   static_cast<long>(keepalive_interval_sec));
  // Allow upto 1200 seconds idle time.
  curl_easy_setopt(req_handle, CURLOPT_MAXAGE_CONN, 1200L);
  // Set CURLOPT_ACCEPT_ENCODING to an empty string to pass all supported
  // encodings. See https://curl.se/libcurl/c/CURLOPT_ACCEPT_ENCODING.html.
  curl_easy_setopt(req_handle, CURLOPT_ACCEPT_ENCODING, "");

  if (skip_tls_verification_) {
    curl_easy_setopt(req_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(req_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(req_handle, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(req_handle, CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
  } else {
    curl_easy_setopt(req_handle, CURLOPT_CAINFO_BLOB, ca_cert_blob_.c_str());
  }
  // Set HTTP headers.
  if (!request.headers.empty()) {
    curl_easy_setopt(req_handle, CURLOPT_HTTPHEADER,
                     curl_request_data->headers_list_ptr);
  }
  return curl_request_data;
}

void MultiCurlHttpFetcherAsync::ScheduleAsyncCurlRequest(
    std::unique_ptr<CurlRequestData> request) {
  // Spreads the requests in a uniformly distributed (random) manner across
  // the available queues/workers.
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  std::uniform_int_distribution<int> distribute(0, num_curl_workers_ - 1);
  int num_worker = distribute(generator);

  auto& request_queue = request_queues_[num_worker];
  absl::MutexLock lock(&request_queue->Mu());
  if (request_queue->Full()) {
    std::move(request->done_callback)(
        absl::ResourceExhaustedError("Request Queue Limit Exceeded."));
    return;
  }

  request_queue->Enqueue(std::move(request));
}

void MultiCurlHttpFetcherAsync::FetchUrl(const HTTPRequest& request,
                                         int timeout_ms,
                                         OnDoneFetchUrl done_callback) {
  FetchUrlWithMetadata(request, timeout_ms,
                       [callback = std::move(done_callback)](
                           absl::StatusOr<HTTPResponse> response) mutable {
                         if (response.ok()) {
                           absl::StatusOr<std::string> string_response =
                               std::move(response->body);
                           std::move(callback)(std::move(string_response));
                         } else {
                           std::move(callback)(response.status());
                         }
                       });
}

void MultiCurlHttpFetcherAsync::FetchUrlWithMetadata(
    const HTTPRequest& request, int timeout_ms,
    OnDoneFetchUrlWithMetadata done_callback) {
  ScheduleAsyncCurlRequest(
      CreateCurlRequest(request, timeout_ms, keepalive_idle_sec_,
                        keepalive_interval_sec_, std::move(done_callback)));
}

void MultiCurlHttpFetcherAsync::PutUrl(const HTTPRequest& http_request,
                                       int timeout_ms,
                                       OnDoneFetchUrl done_callback) {
  absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>)> on_done_with_metadata =
      [callback = std::move(done_callback)](
          absl::StatusOr<HTTPResponse> response) mutable {
        if (response.ok()) {
          absl::StatusOr<std::string> string_response =
              std::move(response->body);
          std::move(callback)(std::move(string_response));
        } else {
          std::move(callback)(response.status());
        }
      };
  auto request = CreateCurlRequest(http_request, timeout_ms,
                                   keepalive_idle_sec_, keepalive_interval_sec_,
                                   std::move(on_done_with_metadata));

  request->body =
      std::make_unique<DataToUpload>(DataToUpload{http_request.body});
  curl_easy_setopt(request->req_handle, CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(request->req_handle, CURLOPT_PUT, 1L);
  curl_easy_setopt(request->req_handle, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(http_request.body.size()));
  curl_easy_setopt(request->req_handle, CURLOPT_READDATA, request->body.get());
  curl_easy_setopt(request->req_handle, CURLOPT_READFUNCTION, ReadCallback);

  ScheduleAsyncCurlRequest(std::move(request));
}

}  // namespace privacy_sandbox::bidding_auction_servers
