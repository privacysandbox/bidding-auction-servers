//  Copyright 2023 Google LLC
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

#include "services/auction_service/code_wrapper/seller_code_wrapper.h"

#include <regex>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
void AppendFeatureFlagValue(std::string& feature_flags,
                            absl::string_view feature_name,
                            bool is_feature_enabled) {
  absl::string_view enable_feature = kFeatureDisabled;
  if (is_feature_enabled) {
    enable_feature = kFeatureEnabled;
  }
  feature_flags.append(
      absl::StrCat("\"", feature_name, "\": ", enable_feature));
}
}  // namespace

// Returns the reportWin wrapper function that should be called from
// reportingEntryFunction. The function name is
// <sanitized_buyer_origin>_reportWinWrapper buyer_origin is sanitized by
// removing the special characters Example: if buyer_origin is http://buyer.com,
// the function returns httpbuyercom_reportWinWrapper.
// This will ensure that each of the buyer's reportWin() function code has a
// different wrapper.
std::string GetReportWinFunctionName(absl::string_view buyer_origin) {
  std::string buyer_prefix{buyer_origin};
  std::regex re("[^a-zA-Z0-9]");
  return absl::StrCat(kReportWinWrapperFunctionName,
                      std::regex_replace(buyer_prefix, re, ""));
}

void ReplacePlaceholders(std::string& report_win_wrapper_template,
                         absl::string_view report_win_wrapper_name,
                         absl::string_view report_win_code) {
  report_win_wrapper_template.replace(
      report_win_wrapper_template.find(kReportWinWrapperNamePlaceholder),
      strlen(kReportWinWrapperNamePlaceholder), report_win_wrapper_name);
  report_win_wrapper_template.replace(
      report_win_wrapper_template.find(kReportWinCodePlaceholder),
      strlen(kReportWinCodePlaceholder), report_win_code);
}

std::string GetSellerWrappedCode(
    absl::string_view seller_js_code, bool enable_report_result_url_generation,
    bool enable_report_win_url_generation,
    const absl::flat_hash_map<std::string, std::string>&
        buyer_origin_code_map) {
  std::string wrap_code{absl::StrCat(kEntryFunction, seller_js_code)};
  if (enable_report_result_url_generation) {
    wrap_code.append(kReportingEntryFunction);
  }
  if (enable_report_win_url_generation) {
    for (const auto& [buyer_origin, buyer_code] : buyer_origin_code_map) {
      std::string reporting_code{kReportingWinWrapperTemplate};
      ReplacePlaceholders(reporting_code,
                          GetReportWinFunctionName(buyer_origin), buyer_code);
      wrap_code.append(reporting_code);
    }
  }
  return wrap_code;
}

std::string GetFeatureFlagJson(bool enable_logging,
                               bool enable_debug_url_generation) {
  std::string feature_flags = "{";
  AppendFeatureFlagValue(feature_flags, kFeatureLogging, enable_logging);
  feature_flags.append(",");
  AppendFeatureFlagValue(feature_flags, kFeatureDebugUrlGeneration,
                         enable_debug_url_generation);
  feature_flags.append("}");
  return feature_flags;
}
}  // namespace privacy_sandbox::bidding_auction_servers
