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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_LOG_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_LOG_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log_sink.h"
#include "absl/synchronization/mutex.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// The INFERENCE_LOG marcro is used to log to an InferenceDebugInfo proto.
#define INFERENCE_LOG(level, request_context) \
  ABSL_LOG(level).ToSinkOnly(request_context.LogSink())

// This class wraps a consented debugging log sink which redirect logs to an
// InferenceDebugInfo proto. When ABSL_LOG is invoked on the log sink, logging
// can be redirected to it based on user consent. We currently return the
// InferenceDebugInfo proto with a PredictResponse to the caller of
// InferenceService
class RequestContext {
 public:
  RequestContext(absl::AnyInvocable<InferenceDebugInfo*()> debug_info,
                 bool is_consented)
      : log_sink_(std::make_unique<DebugResponseSinkImpl>(std::move(debug_info),
                                                          is_consented)) {}

  RequestContext() : log_sink_(std::make_unique<NoOpSinkImpl>()) {}

  absl::LogSink* LogSink() const { return log_sink_.get(); }

 private:
  class DebugResponseSinkImpl : public absl::LogSink {
   public:
    DebugResponseSinkImpl(absl::AnyInvocable<InferenceDebugInfo*()> debug_info,
                          bool is_consented)
        : debug_info_(std::move(debug_info)), is_consented_(is_consented) {}

    void Send(const absl::LogEntry& entry) override
        ABSL_LOCKS_EXCLUDED(mutex_) {
      if (is_consented_) {
        absl::MutexLock lock(&mutex_);
        // Resolves Tensorflow proto version compatibility.
        debug_info_()->add_logs(std::string(entry.text_message_with_prefix()));
      }
    }

    void Flush() override {}

   private:
    absl::AnyInvocable<InferenceDebugInfo*()> debug_info_;
    const bool is_consented_;
    absl::Mutex mutex_;
  };

  class NoOpSinkImpl : public absl::LogSink {
   public:
    void Send(const absl::LogEntry& entry) override {}

    void Flush() override {}
  };
  std::unique_ptr<absl::LogSink> log_sink_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_LOG_H_
