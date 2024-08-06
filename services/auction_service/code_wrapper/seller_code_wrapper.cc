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
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/absl_log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/common/util/reporting_util.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using ProcessReportWinBlob = absl::AnyInvocable<void(
    absl::string_view, absl::string_view, std::string&)>;
using TagToValue = std::vector<std::pair<absl::string_view, absl::string_view>>;

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

std::string GetProtectedAppSignalsReportWinFunctionName(
    absl::string_view buyer_origin) {
  return absl::StrCat(GetReportWinFunctionName(buyer_origin),
                      kProtectedAppSignalsTag);
}

void ReplacePlaceholders(std::string& target_template,
                         const TagToValue& tag_to_value) {
  for (auto [tag, value] : tag_to_value) {
    auto pos = target_template.find(tag);
    while (pos != std::string::npos) {
      target_template.replace(pos, strlen(tag.data()), value);
      pos = target_template.find(tag);
    }
  }
}

std::string GetSellerWrappedCodeHelper(
    bool enable_report_result_url_generation,
    bool enable_report_win_url_generation,
    const absl::flat_hash_map<std::string, std::string>& buyer_origin_code_map,
    const TagToValue& tag_to_value,
    ProcessReportWinBlob process_report_win_blob) {
  std::string wrap_code;
  if (enable_report_result_url_generation) {
    std::string reporting_entry_code{kReportingEntryFunction};
    ReplacePlaceholders(reporting_entry_code, tag_to_value);
    wrap_code.append(reporting_entry_code);
  }
  if (enable_report_win_url_generation) {
    for (const auto& [buyer_origin, buyer_code] : buyer_origin_code_map) {
      std::string reporting_code{kReportingWinWrapperTemplate};
      process_report_win_blob(buyer_origin, buyer_code, reporting_code);
      wrap_code.append(reporting_code);
    }
  }
  return wrap_code;
}

}  // namespace

std::string GetSellerWrappedCode(
    absl::string_view seller_js_code, bool enable_report_result_url_generation,
    bool enable_report_win_url_generation,
    const absl::flat_hash_map<std::string, std::string>&
        buyer_origin_code_map) {
  return GetSellerWrappedCode(
      seller_js_code, enable_report_result_url_generation,
      /*enable_protected_app_signals=*/false, enable_report_win_url_generation,
      buyer_origin_code_map,
      /*protected_app_signals_buyer_origin_code_map=*/{});
}

std::string GetSellerWrappedCode(
    absl::string_view seller_js_code, bool enable_report_result_url_generation,
    bool enable_protected_app_signals, bool enable_report_win_url_generation,
    const absl::flat_hash_map<std::string, std::string>& buyer_origin_code_map,
    const absl::flat_hash_map<std::string, std::string>&
        protected_app_signals_buyer_origin_code_map) {
  std::string wrap_code{kEntryFunction};
  if (enable_protected_app_signals) {
    ABSL_LOG(INFO) << "Protected app signals are enabled, appending the "
                      "protected app signals report win code blob";
    wrap_code.append(GetSellerWrappedCodeHelper(
        enable_report_result_url_generation, enable_report_win_url_generation,
        protected_app_signals_buyer_origin_code_map,
        {{kSuffix, kProtectedAppSignalsTag}, {kExtraArgs, kEgressPayloadTag}},
        [](absl::string_view buyer_origin, absl::string_view buyer_code,
           std::string& report_win_blob) {
          ReplacePlaceholders(
              report_win_blob,
              {{kReportWinWrapperNamePlaceholder,
                GetProtectedAppSignalsReportWinFunctionName(buyer_origin)},
               {kReportWinCodePlaceholder, buyer_code},
               {kExtraArgs, kEgressPayloadTag},
               {"reportWin(", "reportWinProtectedAppSignals("},
               {"reportWin =", "reportWinProtectedAppSignals ="}});
        }));
  }
  wrap_code.append(GetSellerWrappedCodeHelper(
      enable_report_result_url_generation, enable_report_win_url_generation,
      buyer_origin_code_map, {{kSuffix, ""}, {kExtraArgs, ""}},
      [](absl::string_view buyer_origin, absl::string_view buyer_code,
         std::string& report_win_blob) {
        ReplacePlaceholders(report_win_blob,
                            {{kReportWinWrapperNamePlaceholder,
                              GetReportWinFunctionName(buyer_origin)},
                             {kReportWinCodePlaceholder, buyer_code},
                             {kExtraArgs, ""}});
      }));
  wrap_code.append(seller_js_code);
  return wrap_code;
}

}  // namespace privacy_sandbox::bidding_auction_servers
