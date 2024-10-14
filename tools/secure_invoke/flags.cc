// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tools/secure_invoke/flags.h"

ABSL_FLAG(
    std::string, input_file, "",
    "Complete path to the file containing unencrypted request"
    "The file may contain JSON or protobuf based GetBidsRequest payload.");

ABSL_FLAG(std::string, input_format, "JSON",
          "Format of request in the input file. Valid values: JSON, PROTO "
          "(Note: for SelectAdRequest to SFE only JSON is supported)");

ABSL_FLAG(std::string, json_input_str, "",
          "The unencrypted JSON request to be used. If provided, this will be "
          "used to send request rather than loading it from an input file");

ABSL_FLAG(std::string, op, "",
          "The operation to be performed - invoke/encrypt.");

ABSL_FLAG(std::string, host_addr, "",
          "The address for the SellerFrontEnd server to be invoked.");

ABSL_FLAG(std::string, client_ip, "",
          "The IP for the B&A client to be forwarded to KV servers.");

ABSL_FLAG(std::string, client_accept_language, "en-US,en;q=0.9",
          "The accept-language header for the B&A client to be forwarded to KV "
          "servers.");

ABSL_FLAG(
    std::string, client_user_agent,
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36",
    "The user-agent header for the B&A client to be forwarded to KV servers.");

ABSL_FLAG(std::string, client_type, "",
          "Client type (browser or android) to use for request (defaults to "
          "browser)");

ABSL_FLAG(bool, insecure, false,
          "Set to true to send a request to an SFE server running with "
          "insecure credentials");

ABSL_FLAG(std::string, target_service, "SFE",
          "Service name to which the request must be sent to.");

// Coordinator key defaults correspond to defaults in
// https://github.com/privacysandbox/data-plane-shared-libraries/blob/e293c1bdd52e3cf3c0735cd182183eeb8ebf032d/src/encryption/key_fetcher/fake_key_fetcher_manager.h#L29C34-L29C34
ABSL_FLAG(std::string, public_key,
          "87ey8XZPXAd+/+ytKv2GFUWW5j9zdepSJ2G4gebDwyM=",
          "Use exact output from the coordinator. Public key. Must be base64."
          " Defaults to a test public key");
ABSL_FLAG(std::string, private_key,
          "57KS9J3yi4BlmSzerbydAyoOCehHbLbY1QchLnvjubQ=",
          "Required for component server auctions. "
          "Use exact output from the coordinator. Private key. Must be base64."
          " Defaults to a test private key");
ABSL_FLAG(std::string, key_id, "4000000000000000",
          "Use exact output from the coordinator. Hexadecimal key id string "
          "with trailing zeros. Defaults to a test key ID.");

ABSL_FLAG(bool, enable_debug_reporting, false,
          "Set to true to send a request to an SFE server with "
          "debug reporting enabled for this request");

ABSL_FLAG(std::optional<bool>, enable_debug_info, std::nullopt,
          "Set to true to send a request to an SFE server with "
          "DebugInfo enabled for this request");

ABSL_FLAG(std::string, pas_buyer_input_json, "",
          "PAS specific buyer input for each buyer app signal proto.");

ABSL_FLAG(std::optional<bool>, enable_unlimited_egress, std::nullopt,
          "Set to true to indicate to server that temporary unlimited egress "
          "should be enabled");
