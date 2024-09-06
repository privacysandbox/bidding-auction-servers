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

#include "services/auction_service/reporting/reporting_test_util.h"

#include <string>
#include <vector>

#include "rapidjson/document.h"
#include "services/auction_service/reporting/reporting_helper.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
inline void SetAdTechLogs(const std::vector<std::string>& logs,
                          const rapidjson::GenericStringRef<char>& tag,
                          rapidjson::Document& doc) {
  rapidjson::Value log_value(rapidjson::kArrayType);
  for (const std::string& log : logs) {
    rapidjson::Value value(log.c_str(), log.size(), doc.GetAllocator());
    log_value.PushBack(value, doc.GetAllocator());
  }
  doc.AddMember(tag, log_value, doc.GetAllocator());
}
}  // namespace
void SetAdTechLogs(const ReportingResponseLogs& console_logs,
                   rapidjson::Document& doc) {
  SetAdTechLogs(console_logs.logs, kReportingUdfLogs, doc);
  SetAdTechLogs(console_logs.warnings, kReportingUdfWarnings, doc);
  SetAdTechLogs(console_logs.errors, kReportingUdfErrors, doc);
}

}  // namespace privacy_sandbox::bidding_auction_servers
