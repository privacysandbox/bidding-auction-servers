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

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>

#include <curl/curl.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "event2/thread.h"
#include "services/common/loggers/request_log_context.h"

namespace privacy_sandbox::bidding_auction_servers {
using ::grpc_event_engine::experimental::EventEngine;

namespace {

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
  curl_off_t queue_time_us = -1;
};
void GetTraceFromCurl(CURL* handle) {
  if (server_common::log::PS_VLOG_IS_ON(kStats)) {
    CurlTimeStats curl_time_stats;
    char* request_url = nullptr;
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
    curl_easy_getinfo(handle, CURLINFO_QUEUE_TIME_T,
                      &curl_time_stats.queue_time_us);

    PS_VLOG(kStats)
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
        << "new_conns:  " << curl_time_stats.new_conns << "\n"
        << "queue duration: "
        << absl::ToDoubleMilliseconds(
               absl::Microseconds(curl_time_stats.queue_time_us))
        << " ms\n";
  }
}

}  // namespace

EventBase::EventBase(int num_priorities) {
  evthread_use_pthreads();
  event_base_ = event_base_new();
  event_base_priority_init(event_base_, num_priorities);
  if (server_common::log::PS_VLOG_IS_ON(10)) {
    event_enable_debug_mode();
  }
}

EventBase::~EventBase() {
  if (event_base_ != nullptr) {
    event_base_free(event_base_);
  }
}

struct event_base* EventBase::get() { return event_base_; }

Event::Event(struct event_base* base, evutil_socket_t fd, short event_type,
             EventCallback event_callback, void* arg, int priority,
             struct timeval* event_timeout)
    : priority_(priority),
      event_(event_new(base, fd, event_type, event_callback, arg)) {
  event_priority_set(event_, priority_);
  event_add(event_, event_timeout);
}

struct event* Event::get() { return event_; }
Event::~Event() {
  if (event_) {
    event_del(event_);
    event_free(event_);
  }
}

static struct timeval OneSecond = {1, 0};

MultiCurlHttpFetcherAsync::MultiCurlHttpFetcherAsync(
    server_common::Executor* executor, int64_t keepalive_interval_sec,
    int64_t keepalive_idle_sec)
    : executor_(executor),
      keepalive_idle_sec_(keepalive_idle_sec),
      keepalive_interval_sec_(keepalive_interval_sec),
      // Shutdown timer event is persistent because we don't want to remove
      // it from the event loop the first time it fires. With this timer, we
      // periodically check for fetcher shutdown and terminate the event loop
      // if fetcher has been shutdown.
      shutdown_timer_event_(Event(event_base_.get(), /*fd=*/-1,
                                  /*event_type=*/EV_PERSIST,
                                  /*event_callback=*/ShutdownEventLoop,
                                  /*arg=*/this,
                                  /*priority=*/0, &OneSecond)),
      multi_curl_request_manager_(event_base_.get()),
      multi_timer_event_(Event(
          event_base_.get(), /*fd=*/-1, /*event_type=*/0,
          /*event_callback=*/multi_curl_request_manager_.MultiTimerCallback,
          /*arg=*/&multi_curl_request_manager_)) {
  multi_curl_request_manager_.Configure([this]() { PerformCurlUpdate(); },
                                        multi_timer_event_.get());
  // Start execution loop.
  executor_->Run([this]() {
    PS_VLOG(5) << "libevent scheduled the event loop";
    event_base_dispatch(event_base_.get());
  });
}

void MultiCurlHttpFetcherAsync::ShutdownEventLoop(int fd, short event_type,
                                                  void* arg) {
  auto* self = reinterpret_cast<MultiCurlHttpFetcherAsync*>(arg);
  if (!self->shutdown_requested_.HasBeenNotified()) {
    return;
  }

  PS_VLOG(5) << "Shutting down the event loop";
  event_base_loopbreak(self->event_base_.get());
  self->shutdown_complete_.Notify();
}

MultiCurlHttpFetcherAsync::~MultiCurlHttpFetcherAsync()
    ABSL_LOCKS_EXCLUDED(in_loop_mu_, curl_handle_set_lock_) {
  // Notify other threads about shutdown.
  shutdown_requested_.Notify();
  shutdown_complete_.WaitForNotification();
  // We ensure that no other thread will lock callback_map_lock_ and in_loop_mu_
  // here since no new requests are being accepted, or processed through
  // the execution loop.
  absl::MutexLock lock(&curl_handle_set_lock_);

  // Execute all callbacks and clean up handles
  for (auto& handle : curl_handle_set_) {
    multi_curl_request_manager_.Remove(handle);
    CurlRequestData* output;
    curl_easy_getinfo(handle, CURLINFO_PRIVATE, &output);
    std::unique_ptr<CurlRequestData> curl_request_data_ptr(output);
    // Server is shutting down, so exiting gracefully.
    if (output == nullptr) {
      ABSL_LOG(ERROR) << "Curl Error: Pointer to Curl data lost";
      continue;
    }
    std::move(output->done_callback)(
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
    FetchUrl(requests.at(i), absl::ToInt64Milliseconds(timeout),
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

std::unique_ptr<MultiCurlHttpFetcherAsync::CurlRequestData>
MultiCurlHttpFetcherAsync::CreateCurlRequest(
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
  curl_easy_setopt(req_handle, CURLOPT_PRIVATE, curl_request_data.get());
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

  // Set HTTP headers.
  if (!request.headers.empty()) {
    curl_easy_setopt(req_handle, CURLOPT_HTTPHEADER,
                     curl_request_data->headers_list_ptr);
  }
  return curl_request_data;
}

void MultiCurlHttpFetcherAsync::ExecuteCurlRequest(
    std::unique_ptr<CurlRequestData> request) {
  // If shutdown has been initiated while we were preparing/adding request.
  if (shutdown_requested_.HasBeenNotified()) {
    std::move(request->done_callback)(
        absl::InternalError("Client is shutting down."));
    return;
  }
  // Check for errors from multi handle here and execute callback immediately.
  auto* req_handle = request->req_handle;
  CURLMcode mc = multi_curl_request_manager_.Add(req_handle);
  switch (mc) {
    case CURLM_CALL_MULTI_PERFORM:
    case CURLM_OK:
    case CURLM_ADDED_ALREADY:
    case CURLM_RECURSIVE_API_CALL:
    case CURLM_LAST:
      Add(req_handle);
      // Release request data ownership so it can be tracked
      // completely through the curl easy handle. This will be manually cleaned
      // when the request completes or when this class is destroyed.
      request.release();  // NOLINT
      return;
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
    default:
      std::move(request->done_callback)(absl::InternalError(
          absl::StrCat("Failed to invoke request via curl with error ",
                       curl_multi_strerror(mc))));
      return;
  }
}

void MultiCurlHttpFetcherAsync::FetchUrl(const HTTPRequest& request,
                                         int timeout_ms,
                                         OnDoneFetchUrl done_callback) {
  FetchUrl(request, timeout_ms,
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

void MultiCurlHttpFetcherAsync::FetchUrl(
    const HTTPRequest& request, int timeout_ms,
    OnDoneFetchUrlWithMetadata done_callback) {
  ExecuteCurlRequest(CreateCurlRequest(request, timeout_ms, keepalive_idle_sec_,
                                       keepalive_interval_sec_,
                                       std::move(done_callback)));
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

  ExecuteCurlRequest(std::move(request));
}

std::pair<absl::Status, void*> MultiCurlHttpFetcherAsync::GetResultFromMsg(
    CURLMsg* msg) {
  void* output;
  absl::Status status;
  curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &output);
  if (msg->msg == CURLMSG_DONE) {
    auto result_msg = curl_easy_strerror(msg->data.result);
    switch (msg->data.result) {
      case CURLE_OK: {
        long http_code = 400;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code >= 400) {
          char* request_url = nullptr;
          curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL,
                            &request_url);
          status = absl::InternalError(absl::StrCat(
              kFailCurl, " HTTP Code: ", http_code, "; ", request_url));
        } else {
          status = absl::OkStatus();
        }
      } break;
      case CURLE_OPERATION_TIMEDOUT:
        status = absl::DeadlineExceededError(result_msg);
        break;
      case CURLE_URL_MALFORMAT:
        status = absl::InvalidArgumentError(result_msg);
        break;
      default:
        status = absl::InternalError(result_msg);
        break;
    }
  } else {
    status = absl::InternalError(
        absl::StrCat("Failed to read message via curl with error: ", msg->msg));
  }
  return std::make_pair(status, output);
}

void MultiCurlHttpFetcherAsync::PerformCurlUpdate()
    ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_) {
  // Check for updates (provide computation for Libcurl to perform I/O).
  int msgs_left = -1;
  while (CURLMsg* msg = multi_curl_request_manager_.GetUpdate(&msgs_left)) {
    PS_VLOG(10) << __func__ << ": A curl handle completed transfer";
    // Get data for completed message.
    auto [status, data_ptr] = GetResultFromMsg(msg);
    multi_curl_request_manager_.Remove(msg->easy_handle);
    // Must be called before the handle has been cleaned up.
    // The cleanup happens at the end of the lambda in the next block when the
    // std::unique_ptr<CurlRequestData> object goes out of scope.
    Remove(msg->easy_handle);
    // Execute callback in another thread.
    executor_->Run([req_handle = msg->easy_handle, status = status,
                    data_ptr = data_ptr]() mutable {
      // If this happens, then we've effectively lost the reactor that made
      // this call and this memory has leaked.
      if (data_ptr == nullptr) {
        ABSL_LOG(ERROR) << "Curl Error: Pointer to Curl data lost with status: "
                        << status.message()
                        << ". Memory for this call has leaked.";
        return;
      }
      std::unique_ptr<CurlRequestData> curl_request_data_ptr(
          static_cast<CurlRequestData*>(data_ptr));
      if (!curl_request_data_ptr->response_headers.empty()) {
        struct curl_header* curl_header_ptr;
        for (std::string& header : curl_request_data_ptr->response_headers) {
          if (header.empty()) {
            continue;
          }
          CURLHcode header_result = curl_easy_header(
              req_handle, &(header[0]),
              /*first instance of header=*/0, CURLH_HEADER,
              /*last request in case of redirects=*/-1, &curl_header_ptr);
          if (header_result != 0) {
            curl_request_data_ptr->response_with_metadata.headers.emplace(
                std::move(header),
                // https://curl.se/libcurl/c/libcurl-errors.html
                absl::InternalError(absl::StrCat(
                    "Error while fetching Curl header, CURLHcode: ",
                    header_result)));
          } else {
            curl_request_data_ptr->response_with_metadata.headers.emplace(
                std::move(header), curl_header_ptr->value);
          }
        }
      }
      if (curl_request_data_ptr->include_redirect_url) {
        char* final_url;
        curl_easy_getinfo(req_handle, CURLINFO_EFFECTIVE_URL, &final_url);
        // final_url memory gets freed in curl_easy_cleanup.
        // Must be copied to prevent double destruction by CURL and HTTPResponse
        // destructor.
        curl_request_data_ptr->response_with_metadata.final_url =
            absl::StrCat(final_url);
      }
      // invoke callback for handle.
      if (status.ok()) {
        PS_VLOG(10) << "Invoking callback for successful curl operation";
        std::move(curl_request_data_ptr->done_callback)(
            std::move(curl_request_data_ptr->response_with_metadata));
      } else {
        std::move(curl_request_data_ptr->done_callback)(status);
      }
      GetTraceFromCurl(req_handle);
      // perform cleanup for handle.
    });
  }
}
void MultiCurlHttpFetcherAsync::Add(CURL* handle) {
  // Add request handle to set if required for cleanup.
  absl::MutexLock lock(&curl_handle_set_lock_);
  curl_handle_set_.emplace(handle);
}
void MultiCurlHttpFetcherAsync::Remove(CURL* handle) {
  absl::MutexLock lock(&curl_handle_set_lock_);
  curl_handle_set_.erase(handle);
}

MultiCurlHttpFetcherAsync::CurlRequestData::CurlRequestData(
    const std::vector<std::string>& headers, OnDoneFetchUrlWithMetadata on_done,
    std::vector<std::string> response_header_keys, bool include_redirect_url)
    : req_handle(curl_easy_init()),
      done_callback(std::move(on_done)),
      response_headers(std::move(response_header_keys)),
      include_redirect_url(include_redirect_url) {
  for (const auto& header : headers) {
    headers_list_ptr = curl_slist_append(headers_list_ptr, header.c_str());
  }
}

MultiCurlHttpFetcherAsync::CurlRequestData::~CurlRequestData() {
  curl_slist_free_all(headers_list_ptr);
  curl_easy_cleanup(req_handle);
}
}  // namespace privacy_sandbox::bidding_auction_servers
