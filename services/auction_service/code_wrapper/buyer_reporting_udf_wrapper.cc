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

#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"

#include <string>
#include <utility>

#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "services/auction_service/code_wrapper/generated_private_aggregation_wrapper.h"

namespace privacy_sandbox::bidding_auction_servers {

std::string GetBuyerWrappedCode(absl::string_view buyer_js_code,
                                bool enable_protected_app_signals,
                                bool enable_private_aggregate_reporting) {
  std::string wrap_code{kReportWinWrapperFunction};
  if (enable_protected_app_signals) {
    wrap_code = absl::StrReplaceAll(wrap_code, {{"$extraArgs", kEgressArgs}});
  }
  wrap_code.append(buyer_js_code);
  if (!enable_protected_app_signals && enable_private_aggregate_reporting) {
    wrap_code.append(kPrivateAggregationWrapperFunction);
  }
  return wrap_code;
}

}  // namespace privacy_sandbox::bidding_auction_servers
