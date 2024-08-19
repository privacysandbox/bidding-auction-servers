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

#ifndef SERVICES_BIDDING_SERVICE_CODE_WRAPPER_PRIVATE_AGGREGATION_WRAPPER_H_
#define SERVICES_BIDDING_SERVICE_CODE_WRAPPER_PRIVATE_AGGREGATION_WRAPPER_H_

#include <string>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Used by expand_js_to_header genrule to generate the final js code with
// wrapper for Private Aggregation API support
constexpr absl::string_view kPrivateAggregationWrapperFunction = R"JS_CODE(
%JS_CODE_PLACEHOLDER%
)JS_CODE";

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_CODE_WRAPPER_PRIVATE_AGGREGATION_WRAPPER_H_
