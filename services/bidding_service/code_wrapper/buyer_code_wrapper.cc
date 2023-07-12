
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
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"

#include <string>

namespace privacy_sandbox::bidding_auction_servers {

std::string GetBuyerWrappedCode(absl::string_view adtech_code_blob) {
  return absl::StrCat(kEntryFunction, adtech_code_blob);
}
}  // namespace privacy_sandbox::bidding_auction_servers
