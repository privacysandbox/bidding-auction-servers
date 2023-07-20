
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
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

std::string WasmBytesToJavascript(absl::string_view wasm_bytes) {
  std::string hex_array = "";

  for (const uint8_t& byte :
       std::vector<uint8_t>(wasm_bytes.begin(), wasm_bytes.end())) {
    absl::StrAppend(&hex_array, absl::StrFormat("%#x,", byte));
  }
  // In javascript, it is ok to leave a comma after the last element in the
  // array.

  return absl::StrFormat(kWasmModuleTemplate, hex_array);
}
}  // namespace

std::string GetBuyerWrappedCode(absl::string_view adtech_js,
                                absl::string_view adtech_wasm = "") {
  return absl::StrCat(WasmBytesToJavascript(adtech_wasm), kEntryFunction,
                      adtech_js);
}
}  // namespace privacy_sandbox::bidding_auction_servers
