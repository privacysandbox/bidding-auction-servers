/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_IMPL_TEST_H_
#define SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_IMPL_TEST_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/exporters/ostream/log_record_exporter.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"
#include "services/common/loggers/request_context_impl.h"
#include "services/common/loggers/request_context_logger_test.h"
#include "services/common/test/utils/proto_utils.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers::log {

namespace logs_api = opentelemetry::logs;
namespace logs_sdk = opentelemetry::sdk::logs;
namespace logs_exporter = opentelemetry::exporter::logs;

class ConsentedLogTest : public LogTest {
 protected:
  void SetUp() override {
    // initialize max verbosity = kMaxV
    PS_VLOG_IS_ON(0, kMaxV);

    logger_ = logs_sdk::LoggerProviderFactory::Create(
        logs_sdk::SimpleLogRecordProcessorFactory::Create(
            std::make_unique<logs_exporter::OStreamLogRecordExporter>(
                GetSs())));
    logger_private = logger_->GetLogger("default").get();

    mismatched_token_ = ParseTextOrDie<ConsentedDebugConfiguration>(R"pb(
      is_consented: true
      token: "mismatched_Token"
    )pb");

    matched_token_ = ParseTextOrDie<ConsentedDebugConfiguration>(R"pb(
      is_consented: true
      token: "server_tok"
    )pb");
  }

  static std::stringstream& GetSs() {
    // never destructed, outlive 'OStreamLogRecordExporter'
    static auto* ss = new std::stringstream();
    return *ss;
  }

  std::string ReadSs() {
    // Shut down reader now to avoid concurrent access of Ss.
    {
      auto not_used = std::move(test_instance_);
      logger_ = nullptr;
    }
    std::string output = GetSs().str();
    GetSs().str("");
    return output;
  }

  std::unique_ptr<logs_api::LoggerProvider> logger_;
  std::unique_ptr<ContextImpl> test_instance_;
  ConsentedDebugConfiguration matched_token_, mismatched_token_;

  const absl::string_view kServerToken = "server_tok";
};

class DebugResponseTest : public ConsentedLogTest {
 protected:
  void SetUp() override {
    ConsentedLogTest::SetUp();
    debug_info_config_.set_is_debug_info_in_response(true);
  }

  SelectAdResponse ad_response_;
  ConsentedDebugConfiguration debug_info_config_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::log
#endif  // SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_IMPL_TEST_H_
