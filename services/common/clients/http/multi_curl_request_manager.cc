// Copyright 2022 Google LLC
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

#include "services/common/clients/http/multi_curl_request_manager.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <event2/event.h>
#include <event2/util.h>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "services/common/loggers/request_log_context.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using EventCallback = void (*)(int, short, void*);
inline static struct timeval ZeroSecond = {0, 0};
inline static struct timeval OneMicrosecond = {0, 1};

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

int OnLibcurlTimerUpdate(CURLM* multi, long timeout_ms, void* timer_event_arg) {
  DCHECK_NE(timer_event_arg, nullptr) << "Timer event not found";
  struct event* timer_event = reinterpret_cast<struct event*>(timer_event_arg);
  if (timeout_ms == -1) {
    evtimer_del(timer_event);
  } else {
    struct timeval timeout = {.tv_sec = timeout_ms / 1000,
                              .tv_usec = (timeout_ms % 1000) * 1000};
    evtimer_add(timer_event, &timeout);
  }
  return 0;
}

// Parses curl message to result string or an error message for the callback.
absl::Status GetResultFromMsg(CURLMsg* msg) {
  absl::Status status;
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
  return status;
}

}  // namespace

void RemoveSocketFromLibevent(std::unique_ptr<SocketInfo> socket_info) {
  if (!socket_info) {
    return;
  }

  if (event_initialized(&socket_info->tracked_event)) {
    // Make the event non-pending and non-active i.e.
    // remove the registered event from the libevent's monitoring.
    event_del(&socket_info->tracked_event);
  }
}

void MultiCurlRequestManager::OnLibeventSocketActivity(int fd, short kind,
                                                       void* data) {
  int action = ((kind & EV_READ) ? CURL_CSELECT_IN : 0) |
               ((kind & EV_WRITE) ? CURL_CSELECT_OUT : 0);

  auto* self = reinterpret_cast<MultiCurlRequestManager*>(data);
  curl_multi_socket_action(self->request_manager_, fd, action,
                           &self->running_handles_);
  self->PerformCurlUpdate();
}

void MultiCurlRequestManager::UpsertSocketInLibevent(curl_socket_t sock_fd,
                                                     int activity,
                                                     SocketInfo* socket_info) {
  int kind = ((activity & CURL_POLL_IN) ? EV_READ : 0) |
             ((activity & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST;

  socket_info->sock_fd = sock_fd;
  socket_info->activity = activity;

  if (event_initialized(&socket_info->tracked_event)) {
    event_del(&socket_info->tracked_event);
  }

  event_assign(&socket_info->tracked_event, event_base_.get(), sock_fd, kind,
               OnLibeventSocketActivity, this);
  event_add(&socket_info->tracked_event, /*timeout=*/nullptr);
}

void MultiCurlRequestManager::AddSocketToLibevent(curl_socket_t sock,
                                                  int activity, void* data) {
  auto socket_info = std::make_unique<SocketInfo>();
  auto* self = reinterpret_cast<MultiCurlRequestManager*>(data);
  self->UpsertSocketInLibevent(sock, activity, socket_info.get());
  curl_multi_assign(self->request_manager_, sock, socket_info.release());
}

int MultiCurlRequestManager::OnLibcurlSocketUpdate(CURL* easy_handle,
                                                   curl_socket_t sock_fd,
                                                   int activity, void* data,
                                                   void* socket_info_pointer) {
  struct SocketInfo* socket_info =
      reinterpret_cast<struct SocketInfo*>(socket_info_pointer);
  auto* self = reinterpret_cast<MultiCurlRequestManager*>(data);
  // See activity details here:
  // https://curl.se/libcurl/c/CURLMOPT_SOCKETFUNCTION.html
  if (activity == CURL_POLL_REMOVE) {
    RemoveSocketFromLibevent(std::unique_ptr<SocketInfo>(socket_info));
  } else if (!socket_info) {
    self->AddSocketToLibevent(sock_fd, activity, data);
  } else {
    self->UpsertSocketInLibevent(sock_fd, activity, socket_info);
  }
  return 0;
}

MultiCurlRequestManager::MultiCurlRequestManager(
    const long curlmopt_maxconnects, const long curlmopt_max_total_connections,
    const long curlmopt_max_host_connections, server_common::Executor& executor)
    : running_handles_(0),
      eventloop_started_event_(Event(event_base_.get(), /* fd= */ -1,
                                     /* event_type= */ EV_TIMEOUT,
                                     /* event_callback= */ StartedEventLoop,
                                     /* arg=*/this,
                                     /* priority= */ kNumEventPriorities / 2,
                                     &OneMicrosecond)),
      // Shutdown timer event is persistent because we don't want to remove
      // it from the event loop the first time it fires. With this timer, we
      // periodically check for fetcher shutdown and terminate the event loop
      // if fetcher has been shutdown.
      shutdown_timer_event_(Event(event_base_.get(), /*fd=*/-1,
                                  /*event_type=*/EV_TIMEOUT,
                                  /*event_callback=*/ShutdownEventLoop,
                                  /*arg=*/this,
                                  /*priority=*/0, &ZeroSecond,
                                  /*on_delete=*/nullptr,
                                  /*add_to_loop=*/false)),
      multi_timer_event_(Event(event_base_.get(), /*fd=*/-1, /*event_type=*/0,
                               /*event_callback=*/MultiTimerCallback,
                               /*arg=*/this)),
      executor_(executor) {
  curl_global_init(CURL_GLOBAL_ALL);
  request_manager_ = curl_multi_init();
  curl_multi_setopt(request_manager_, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(request_manager_, CURLMOPT_SOCKETFUNCTION,
                    OnLibcurlSocketUpdate);
  curl_multi_setopt(request_manager_, CURLMOPT_TIMERDATA,
                    multi_timer_event_.get());
  curl_multi_setopt(request_manager_, CURLMOPT_TIMERFUNCTION,
                    OnLibcurlTimerUpdate);
  if (curlmopt_maxconnects > 0) {
    // Limits number of connections left alive in cache.
    curl_multi_setopt(request_manager_, CURLMOPT_MAXCONNECTS,
                      curlmopt_maxconnects);
  }
  // Limits number of connections allowed active.
  curl_multi_setopt(request_manager_, CURLMOPT_MAX_TOTAL_CONNECTIONS,
                    curlmopt_max_total_connections);
  // The maximum amount of simultaneously open connections libcurl may hold
  // to a single host.
  curl_multi_setopt(request_manager_, CURLMOPT_MAX_HOST_CONNECTIONS,
                    curlmopt_max_host_connections);

  // Start execution loop.
  executor_.Run([this]() {
    event_base_loop(event_base_.get(), EVLOOP_NO_EXIT_ON_EMPTY);
  });
  eventloop_started_.WaitForNotification();
}

MultiCurlRequestManager::~MultiCurlRequestManager() {
  event_add(shutdown_timer_event_.get(), &ZeroSecond);
  event_active(shutdown_timer_event_.get(), 0, 0);
  shutdown_complete_.WaitForNotification();
}

// static
void MultiCurlRequestManager::ShutdownEventLoop(int fd, short event_type,
                                                void* arg) {
  // Cancel all requests.
  auto* self = reinterpret_cast<MultiCurlRequestManager*>(arg);
  self->PerformCurlUpdate();
  curl_multi_cleanup(self->request_manager_);
  curl_global_cleanup();
  event_base_loopbreak(self->event_base_.get());

  self->shutdown_complete_.Notify();
}

// State required to process the activated curl request handling event in the
// event loop.
struct RequestEventState {
  MultiCurlRequestManager& curl_request_manager;
  std::unique_ptr<CurlRequestData> curl_request;
  std::unique_ptr<Event> event;
};

// static
void MultiCurlRequestManager::StartedEventLoop(int fd, short event_type,
                                               void* arg) {
  auto* self = reinterpret_cast<MultiCurlRequestManager*>(arg);
  PS_VLOG(5) << "Notifying event loop start";
  self->eventloop_started_.Notify();
}

// static
void MultiCurlRequestManager::OnProcessingStarted(int fd, short event_type,
                                                  void* arg) {
  auto request_event_state = std::unique_ptr<RequestEventState>(
      reinterpret_cast<RequestEventState*>(arg));
  // Check for errors from multi handle here and execute callback immediately.
  auto* req_handle = request_event_state->curl_request->req_handle;
  auto& curl_request_manager = request_event_state->curl_request_manager;
  CURLMcode mc = curl_request_manager.Add(req_handle);
  switch (mc) {
    case CURLM_CALL_MULTI_PERFORM:
    case CURLM_OK:
    case CURLM_ADDED_ALREADY:
    case CURLM_RECURSIVE_API_CALL:
    case CURLM_LAST:
      curl_request_manager.easy_curl_request_data_[req_handle] =
          std::move(request_event_state->curl_request);
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
      curl_request_manager.executor_.Run(
          [mc, request = std::move(request_event_state->curl_request)]() {
            std::move(request->done_callback)(absl::InternalError(
                absl::StrCat("Failed to invoke request via curl with error ",
                             curl_multi_strerror(mc))));
          });
      return;
  }
  // Event itself is deleted here.
}

void MultiCurlRequestManager::StartProcessing(
    std::unique_ptr<CurlRequestData> request) {
  // Schedules an event on event loop for immediate processing.
  //
  // Note: Rather than directly addding the event to multi-handle here we hop
  // onto event loop to ensure all the multi handling is done inside the same
  // event loop thread and hence obviating the need for managing our own locks.
  auto event =
      std::make_unique<Event>(event_base_.get(), /*fd=*/-1,
                              /*event_type=*/EV_TIMEOUT,
                              /*event_callback=*/OnProcessingStarted,
                              /*arg=*/nullptr,
                              /*priority=*/kNumEventPriorities / 2, &ZeroSecond,
                              /*on_delete=*/nullptr,
                              /*add_to_loop=*/false);
  auto* event_ptr = event.get();
  auto request_event_state =
      std::make_unique<RequestEventState>(RequestEventState{
          .curl_request_manager = *this,
          .curl_request = std::move(request),
          .event = std::move(event),
      });
  // Pass the ownership of the event to the event itself so that it is cleaned
  // up upon activation.
  event_assign(event_ptr->get(), event_base_.get(), /*fd=*/-1,
               /*event_type=*/EV_TIMEOUT,
               /*event_callback=*/OnProcessingStarted,
               /*arg=*/request_event_state.release());
  event_add(event_ptr->get(), &ZeroSecond);
}

void MultiCurlRequestManager::PerformCurlUpdate() {
  // Check for updates (provide computation for Libcurl to perform I/O).
  int msgs_left = -1;
  while (CURLMsg* msg = curl_multi_info_read(request_manager_, &msgs_left)) {
    if (msg->msg != CURLMSG_DONE) {
      continue;
    }

    std::unique_ptr<CurlRequestData> curl_request_data =
        Remove(msg->easy_handle);
    if (curl_request_data == nullptr) {
      // This should never happen.
      continue;
    }

    absl::Status status = GetResultFromMsg(msg);

    // Execute callback in another thread.
    executor_.Run([req_handle = msg->easy_handle, status = std::move(status),
                   curl_request_data = std::move(curl_request_data)]() mutable {
      if (!curl_request_data->response_headers.empty()) {
        struct curl_header* curl_header_ptr;
        for (std::string& header : curl_request_data->response_headers) {
          if (header.empty()) {
            continue;
          }

          CURLHcode header_result = curl_easy_header(
              req_handle, &(header[0]),
              /*first instance of header=*/0, CURLH_HEADER,
              /*last request in case of redirects=*/-1, &curl_header_ptr);
          if (header_result != 0) {
            curl_request_data->response_with_metadata.headers.emplace(
                std::move(header),
                // https://curl.se/libcurl/c/libcurl-errors.html
                absl::InternalError(absl::StrCat(
                    "Error while fetching Curl header, CURLHcode: ",
                    header_result)));
          } else {
            curl_request_data->response_with_metadata.headers.emplace(
                std::move(header), curl_header_ptr->value);
          }
        }
      }
      if (curl_request_data->include_redirect_url) {
        char* final_url;
        curl_easy_getinfo(req_handle, CURLINFO_EFFECTIVE_URL, &final_url);
        // final_url memory gets freed in curl_easy_cleanup.
        // Must be copied to prevent double destruction by CURL and HTTPResponse
        // destructor.
        curl_request_data->response_with_metadata.final_url =
            absl::StrCat(final_url);
      }
      // invoke callback for handle.
      if (status.ok()) {
        std::move(curl_request_data->done_callback)(
            std::move(curl_request_data->response_with_metadata));
      } else {
        std::move(curl_request_data->done_callback)(status);
      }
      GetTraceFromCurl(req_handle);
    });
  }
}

// static
void MultiCurlRequestManager::MultiTimerCallback(int fd, short what,
                                                 void* arg) {
  auto* self = reinterpret_cast<MultiCurlRequestManager*>(arg);
  curl_multi_socket_action(self->request_manager_, CURL_SOCKET_TIMEOUT, 0,
                           &self->running_handles_);
  self->PerformCurlUpdate();
}

CURLMcode MultiCurlRequestManager::Add(CURL* curl_handle) {
  return curl_multi_add_handle(request_manager_, curl_handle);
}

std::unique_ptr<CurlRequestData> MultiCurlRequestManager::Remove(
    CURL* curl_handle) {
  curl_multi_remove_handle(request_manager_, curl_handle);
  auto it = easy_curl_request_data_.find(curl_handle);
  if (it == easy_curl_request_data_.end()) {
    return nullptr;
  }

  std::unique_ptr<CurlRequestData> curl_request_data = std::move(it->second);
  easy_curl_request_data_.erase(it);
  return curl_request_data;
}

}  // namespace privacy_sandbox::bidding_auction_servers
