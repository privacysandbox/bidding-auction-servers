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

#include <iostream>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

std::string GetSellerWrappedCode(absl::string_view seller_js_code,
                                 bool enable_report_result_url_generation) {
  std::string wrap_code{absl::StrCat(kEntryFunction, seller_js_code)};
  if (enable_report_result_url_generation) {
    wrap_code.append(kReportingEntryFunction);
  }
  return wrap_code;
}

}  // namespace privacy_sandbox::bidding_auction_servers
