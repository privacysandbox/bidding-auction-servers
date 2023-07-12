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

#include <string>
#include <utility>

#include <curl/curl.h>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {
using ::grpc_event_engine::experimental::EventEngine;

namespace {
// Parses curl message to result string or an error message for the callback.
absl::StatusOr<std::string> GetResultFromMsg(CURLMsg* msg) {
  std::string* output;
  if (msg->msg == CURLMSG_DONE) {
    auto result_msg = curl_easy_strerror(msg->data.result);
    switch (msg->data.result) {
      case CURLE_OK:
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &output);
        return *output;
      case CURLE_OPERATION_TIMEDOUT:
        return absl::DeadlineExceededError(result_msg);
      case CURLE_URL_MALFORMAT:
        return absl::InvalidArgumentError(result_msg);
      default:
        return absl::InternalError(result_msg);
    }
  } else {
    return absl::InternalError("Failed to read message via curl.");
  }
}

constexpr int log_level = 2;
struct CurlTimeStats {
  double time_namelookup = -1;
  double time_connect = -1;
  double time_appconnect = -1;
  double time_pretransfer = -1;
  double time_redirect = -1;
  double time_starttransfer = -1;
  double time_total = -1;
  curl_off_t download_size = -1;
  curl_off_t upload_size = -1;
  curl_off_t download_speed = -1;
  curl_off_t upload_speed = -1;
  curl_off_t new_conns = -1;
};
void GetTraceFromCurl(CURL* handle) {
  if (VLOG_IS_ON(log_level)) {
    CurlTimeStats curl_time_stats;
    char* request_url = NULL;
    curl_easy_getinfo(handle, CURLINFO_NAMELOOKUP_TIME, &request_url);
    curl_easy_getinfo(handle, CURLINFO_NAMELOOKUP_TIME,
                      &curl_time_stats.time_namelookup);
    curl_easy_getinfo(handle, CURLINFO_CONNECT_TIME,
                      &curl_time_stats.time_connect);
    curl_easy_getinfo(handle, CURLINFO_APPCONNECT_TIME,
                      &curl_time_stats.time_appconnect);
    curl_easy_getinfo(handle, CURLINFO_PRETRANSFER_TIME,
                      &curl_time_stats.time_pretransfer);
    curl_easy_getinfo(handle, CURLINFO_REDIRECT_TIME,
                      &curl_time_stats.time_redirect);
    curl_easy_getinfo(handle, CURLINFO_STARTTRANSFER_TIME,
                      &curl_time_stats.time_starttransfer);
    curl_easy_getinfo(handle, CURLINFO_TOTAL_TIME, &curl_time_stats.time_total);
    curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &request_url);
    curl_easy_getinfo(handle, CURLINFO_SIZE_DOWNLOAD_T,
                      &curl_time_stats.download_size);
    curl_easy_getinfo(handle, CURLINFO_SIZE_UPLOAD_T,
                      &curl_time_stats.upload_size);
    curl_easy_getinfo(handle, CURLINFO_SPEED_DOWNLOAD_T,
                      &curl_time_stats.download_speed);
    curl_easy_getinfo(handle, CURLINFO_SPEED_UPLOAD_T,
                      &curl_time_stats.upload_speed);
    curl_easy_getinfo(handle, CURLINFO_NUM_CONNECTS,
                      &curl_time_stats.new_conns);

    VLOG(log_level)
        << "Curl request " << absl::StrCat(request_url) << " stats: \n"
        << "time_namelookup:  " << curl_time_stats.time_namelookup << "\n"
        << "time_connect:  " << curl_time_stats.time_connect << "\n"
        << "time_appconnect:  " << curl_time_stats.time_appconnect << "\n"
        << "time_pretransfer:  " << curl_time_stats.time_pretransfer << "\n"
        << "time_redirect:  " << curl_time_stats.time_redirect << "\n"
        << "time_starttransfer:  " << curl_time_stats.time_starttransfer << "\n"
        << "time_total:  " << curl_time_stats.time_total << "\n"
        << "download_size:  " << curl_time_stats.download_size << " bytes\n"
        << "upload_size:  " << curl_time_stats.upload_size << " bytes\n"
        << "download_speed:  " << curl_time_stats.download_speed
        << " bytes/second\n"
        << "upload_speed:  " << curl_time_stats.upload_speed
        << " bytes/second\n"
        << "new_conns:  " << curl_time_stats.new_conns << "\n";
  }
}

}  // namespace

MultiCurlHttpFetcherAsync::MultiCurlHttpFetcherAsync(
    server_common::Executor* executor, int64_t keepalive_interval_sec,
    int64_t keepalive_idle_sec)
    : executor_(executor),
      keepalive_idle_sec_(keepalive_idle_sec),
      keepalive_interval_sec_(keepalive_interval_sec) {
  // Start execution loop.
  executor_->Run([this]() { ExecuteLoop(); });
}

MultiCurlHttpFetcherAsync::~MultiCurlHttpFetcherAsync()
    ABSL_LOCKS_EXCLUDED(in_loop_mu_, curl_data_map_lock_) {
  // Notify other threads about shutdown.
  shutdown_requested_.Notify();
  shutdown_complete_.WaitForNotification();
  // We ensure that no other thread will lock callback_map_lock_ and in_loop_mu_
  // here since no new requests are being accepted, or processed through
  // the execution loop.
  absl::MutexLock l1(&in_loop_mu_);
  absl::MutexLock l2(&curl_data_map_lock_);
  // Execute all callbacks and clean up handles
  for (auto& [handle, handle_data] : curl_data_map_) {
    multi_curl_request_manager_.Remove(handle);
    std::move(handle_data->done_callback)(
        absl::InternalError("Request cancelled due to server shutdown."));
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

// FetchUrlsLifetime manages a single FetchUrls request and encapsulates
// all of the data each individual FetchUrl callback will need.
struct FetchUrlsLifetime {
  OnDoneFetchUrls all_done_callback;
  // Results will be passed into all_done_callback.
  std::vector<absl::StatusOr<std::string>> results;
  // This is used to guard pending_results
  // to keep the count accurate when updated by
  // different threads.
  absl::Mutex results_mu;
  int pending_results;
};

void MultiCurlHttpFetcherAsync::FetchUrls(
    const std::vector<HTTPRequest>& requests, absl::Duration timeout,
    OnDoneFetchUrls done_callback) {
  // The FetchUrl lambdas are the owners of the underlying FetchUrlsLifetime and
  // this shared_ptr will be destructed once the last FetchUrl lambda finishes.
  // Using a shared_ptr here allows us to avoid making MultiCurlHttpFetcherAsync
  // the owner of the FetchUrlsLifetime, which would complicate cleanup on
  // MultiCurlHttpFetcherAsync destruction during a pending FetchUrls call.
  std::shared_ptr<FetchUrlsLifetime> shared_lifetime =
      std::make_shared<FetchUrlsLifetime>();
  shared_lifetime->pending_results = requests.size();
  shared_lifetime->all_done_callback = std::move(done_callback);
  shared_lifetime->results =
      std::vector<absl::StatusOr<std::string>>(requests.size());

  for (int i = 0; i < requests.size(); i++) {
    FetchUrl(requests.at(i), absl::ToInt64Milliseconds(timeout),
             [i, shared_lifetime](absl::StatusOr<std::string> result) {
               absl::MutexLock lock_results(&shared_lifetime->results_mu);
               shared_lifetime->results[i] = std::move(result);
               if (--shared_lifetime->pending_results == 0) {
                 std::move(shared_lifetime->all_done_callback)(
                     std::move(shared_lifetime->results));
               }
             });
  }
}

void MultiCurlHttpFetcherAsync::FetchUrl(const HTTPRequest& request,
                                         int timeout_ms,
                                         OnDoneFetchUrl done_callback)
    ABSL_LOCKS_EXCLUDED(curl_data_map_lock_) {
  auto curl_request_data = std::make_unique<CurlRequestData>(
      request.headers, std::move(done_callback));
  CURL* req_handle = curl_request_data->req_handle;
  curl_easy_setopt(req_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(req_handle, CURLOPT_URL, request.url.begin());
  curl_easy_setopt(req_handle, CURLOPT_WRITEDATA,
                   curl_request_data->output.get());
  curl_easy_setopt(req_handle, CURLOPT_PRIVATE,
                   curl_request_data->output.get());
  curl_easy_setopt(req_handle, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt(req_handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  // Enable TCP keep-alive to keep connection warm.
  curl_easy_setopt(req_handle, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(req_handle, CURLOPT_TCP_KEEPIDLE,
                   static_cast<long>(keepalive_idle_sec_));
  curl_easy_setopt(req_handle, CURLOPT_TCP_KEEPINTVL,
                   static_cast<long>(keepalive_interval_sec_));
  // Allow upto 1200 seconds idle time.
  curl_easy_setopt(req_handle, CURLOPT_MAXAGE_CONN, 1200L);
  // Set CURLOPT_ACCEPT_ENCODING to an empty string to pass all supported
  // encodings. See https://curl.se/libcurl/c/CURLOPT_ACCEPT_ENCODING.html.
  curl_easy_setopt(req_handle, CURLOPT_ACCEPT_ENCODING, "");

  // Set HTTP headers.
  if (!request.headers.empty()) {
    curl_easy_setopt(req_handle, CURLOPT_HTTPHEADER,
                     curl_request_data->headers_list_ptr);
  }

  // Check for errors from multi handle here and execute callback immediately.
  CURLMcode mc = multi_curl_request_manager_.Add(req_handle);
  switch (mc) {
    case CURLM_BAD_HANDLE:
    case CURLM_BAD_EASY_HANDLE:
    case CURLM_OUT_OF_MEMORY:
    case CURLM_INTERNAL_ERROR:
    case CURLM_BAD_SOCKET:
    case CURLM_WAKEUP_FAILURE:
    case CURLM_BAD_FUNCTION_ARGUMENT:
    case CURLM_ABORTED_BY_CALLBACK:
    case CURLM_UNRECOVERABLE_POLL:
    case CURLM_UNKNOWN_OPTION:
      std::move(curl_request_data->done_callback)(absl::InternalError(
          absl::StrCat("Failed to invoke request via curl with error ",
                       curl_multi_strerror(mc))));
      return;
    case CURLM_CALL_MULTI_PERFORM:
    case CURLM_OK:
    case CURLM_ADDED_ALREADY:
    case CURLM_RECURSIVE_API_CALL:
    case CURLM_LAST:
      Add(req_handle, std::move(curl_request_data));
      break;
  }
}

void MultiCurlHttpFetcherAsync::Add(
    CURL* handle, std::unique_ptr<CurlRequestData> curl_request_data)
    ABSL_LOCKS_EXCLUDED(curl_data_map_lock_) {
  // If shutdown has been initiated while we were preparing/adding request.
  if (shutdown_requested_.HasBeenNotified()) {
    std::move(curl_request_data->done_callback)(
        absl::InternalError("Client is shutting down."));
    return;
  }

  // Add callback to map.
  absl::MutexLock l(&curl_data_map_lock_);
  curl_data_map_.try_emplace(handle, std::move(curl_request_data));
}

void MultiCurlHttpFetcherAsync::ExecuteLoop() ABSL_LOCKS_EXCLUDED(in_loop_mu_) {
  if (in_loop_mu_.TryLock()) {
    while (!shutdown_requested_.HasBeenNotified()) {
      PerformCurlUpdate();
    }
    // Shut down has been requested so exit.
    shutdown_complete_.Notify();
    in_loop_mu_.Unlock();
  }
  // Another ExecuteLoop is already running.
}

void MultiCurlHttpFetcherAsync::PerformCurlUpdate()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(in_loop_mu_)
        ABSL_LOCKS_EXCLUDED(curl_data_map_lock_) {
  // Check for updates (provide computation for Libcurl to perform I/O).
  int msgs_left = -1;
  while (CURLMsg* msg = multi_curl_request_manager_.GetUpdate(&msgs_left)) {
    multi_curl_request_manager_.Remove(msg->easy_handle);

    // Get data for completed message.
    std::unique_ptr<CurlRequestData> curl_request_data;
    {
      absl::MutexLock lock(&curl_data_map_lock_);
      curl_request_data = std::move(curl_data_map_.at(msg->easy_handle));
      curl_data_map_.erase(msg->easy_handle);
    }

    // Execute callback in another thread.
    executor_->Run(
        [req_handle = msg->easy_handle, result = GetResultFromMsg(msg),
         curl_request_data = std::move(curl_request_data)]() mutable {
          // invoke callback for handle.
          std::move(curl_request_data->done_callback)(result);
          // perform cleanup for handle.
          GetTraceFromCurl(req_handle);
        });
  }
}

MultiCurlHttpFetcherAsync::CurlRequestData::CurlRequestData(
    const std::vector<std::string>& headers, OnDoneFetchUrl on_done) {
  // Space for the fetch output must be heap allocated.
  // It can (potentially) be multiple megabytes in size, and many simultaneous
  // requests can be in flight due to the async nature of FetchUrl.
  // See CurlStateCleanup for all cleanup.
  output = std::make_unique<std::string>();
  req_handle = curl_easy_init();
  done_callback = std::move(on_done);
  for (const auto& header : headers) {
    headers_list_ptr = curl_slist_append(headers_list_ptr, header.c_str());
  }
}
MultiCurlHttpFetcherAsync::CurlRequestData::~CurlRequestData() {
  curl_slist_free_all(headers_list_ptr);
  curl_easy_cleanup(req_handle);
}
}  // namespace privacy_sandbox::bidding_auction_servers
