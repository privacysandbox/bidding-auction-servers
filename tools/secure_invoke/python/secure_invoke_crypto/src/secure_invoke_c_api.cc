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

#include "tools/secure_invoke/secure_invoke_c_api.h"
#include "tools/secure_invoke/secure_invoke_lib.h"

#include <cstring>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "src/encryption/key_fetcher/fake_key_fetcher_manager.h"
#include "tools/secure_invoke/payload_generator/payload_packaging.h"
#include "services/common/clients/async_grpc/grpc_client_utils.h"
#include "src/encryption/key_fetcher/key_fetcher_utils.h"
#include "absl/strings/escaping.h"
#include <google/protobuf/util/json_util.h>

namespace {
    // Global storage for string results to ensure they remain valid
    thread_local std::string last_response;
    thread_local std::string last_error;
}

extern "C" {

int secure_invoke_init() {
    try {
        // Initialize curl
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            return 0;
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

void secure_invoke_cleanup() {
    curl_global_cleanup();
}

// NOTE: secure_invoke_execute function removed - not needed for crypto-only API

void secure_invoke_free_result(SecureInvokeResult* result) {
    if (result) {
        delete result;
    }
}

const char* secure_invoke_get_version() {
    return "1.0.0";
}

SecureInvokeResult* secure_invoke_encrypt(
    const char* input_json,
    const char* public_key,
    const char* key_id) {
    
    SecureInvokeResult* result = new SecureInvokeResult();
    result->success = 0;
    result->response = nullptr;
    result->error_message = nullptr;
    
    if (!input_json || !public_key || !key_id) {
        last_error = "Missing required parameters: input_json, public_key, or key_id";
        result->error_message = last_error.c_str();
        return result;
    }
    
    try {
        // Process the public key (Base64 decode and convert to hex)
        std::string public_key_bytes;
        if (!absl::Base64Unescape(public_key, &public_key_bytes)) {
            last_error = "Failed to unescape public key";
            result->error_message = last_error.c_str();
            return result;
        }
        std::string public_key_hex = absl::BytesToHexString(public_key_bytes);
        
        // Process the key_id using ToOhttpKeyId
        auto id = privacy_sandbox::server_common::ToOhttpKeyId(key_id);
        if (!id.ok()) {
            last_error = "Failed to process key_id: " + std::string(id.status().message());
            result->error_message = last_error.c_str();
            return result;
        }
        
        // Create HpkeKeyset
        privacy_sandbox::bidding_auction_servers::HpkeKeyset keyset;
        keyset.public_key = std::move(public_key_hex);
        keyset.key_id = static_cast<uint8_t>(std::stoi(key_id));  // Use original key_id directly

        std::cout << "Using key_id: " << static_cast<int>(keyset.key_id) << std::endl;
        std::cout << "Using public_key (hex): " << keyset.public_key << std::endl;
        // Create GetBidsRawRequest and set flags first (like working implementation)
        privacy_sandbox::bidding_auction_servers::GetBidsRequest::GetBidsRawRequest get_bids_raw_request;

        // Parse the input JSON into GetBidsRawRequest
        auto parse_status = google::protobuf::util::JsonStringToMessage(input_json, &get_bids_raw_request);
        if (!parse_status.ok()) {
            last_error = "Failed to parse input JSON: " + std::string(parse_status.message());
            result->error_message = last_error.c_str();
            return result;
        }
        
        // Debug: Check serialized size
        std::string serialized = get_bids_raw_request.SerializeAsString();
        std::cout << "Debug: Serialized payload size: " << serialized.size() << " bytes" << std::endl;
        std::cout << "Debug: GetBidsRawRequest content: " << get_bids_raw_request.DebugString() << std::endl;
        
        // Create crypto components
        auto key_fetcher_manager = std::make_unique<privacy_sandbox::server_common::FakeKeyFetcherManager>(
            keyset.public_key, "unused", std::to_string(keyset.key_id));
        auto crypto_client = privacy_sandbox::bidding_auction_servers::CreateCryptoClient();
        
        // Encrypt the request using the correct template function
        auto secret_request = privacy_sandbox::bidding_auction_servers::EncryptRequestWithHpke<
            privacy_sandbox::bidding_auction_servers::GetBidsRequest>(
            get_bids_raw_request.SerializeAsString(), *crypto_client,
            *key_fetcher_manager, privacy_sandbox::server_common::CloudPlatform::kGcp);
        
        if (!secret_request.ok()) {
            last_error = "Failed to encrypt request: " + std::string(secret_request.status().message());
            result->error_message = last_error.c_str();
            return result;
        }
        
        // Convert to JSON using the proper protobuf method (like secure_invoke_lib.cc:628)
        std::string get_bids_request_json;
        auto get_bids_request_json_status = google::protobuf::util::MessageToJsonString(
            *secret_request->second, &get_bids_request_json);
        if (!get_bids_request_json_status.ok()) {
            last_error = "Failed to convert to JSON: " + std::string(get_bids_request_json_status.message());
            result->error_message = last_error.c_str();
            return result;
        }
        
        // Base64 encode the secret to make it safe for string handling
        std::string encoded_secret;
        absl::Base64Escape(secret_request->first, &encoded_secret);
        
        // Store both the encrypted JSON and the secret (separated by a delimiter)
        last_response = get_bids_request_json + "|||SECRET|||" + encoded_secret;
        result->success = 1;
        result->response = last_response.c_str();
        
    } catch (const std::exception& e) {
        last_error = std::string("Exception in encrypt: ") + e.what();
        result->error_message = last_error.c_str();
    } catch (...) {
        last_error = "Unknown error in encrypt";
        result->error_message = last_error.c_str();
    }
    std::cout << "Returning from encrypt with response: " << last_response << std::endl;
    return result;
}

SecureInvokeResult* secure_invoke_decrypt(
    const char* encrypted_response,
    const char* secret) {
    
    SecureInvokeResult* result = new SecureInvokeResult();
    result->success = 0;
    result->response = nullptr;
    result->error_message = nullptr;
    
    if (!encrypted_response || !secret) {
        last_error = "Missing required parameters: encrypted_response or secret";
        result->error_message = last_error.c_str();
        return result;
    }
    
    try {
        // Decode the base64 encrypted response
        std::string decoded_ciphertext;
        if (!absl::Base64Unescape(encrypted_response, &decoded_ciphertext)) {
            last_error = "Failed to decode base64 encrypted response";
            result->error_message = last_error.c_str();
            return result;
        }
        
        // Decode the base64 secret
        std::string decoded_secret;
        if (!absl::Base64Unescape(secret, &decoded_secret)) {
            last_error = "Failed to decode base64 secret";
            result->error_message = last_error.c_str();
            return result;
        }
        
        // Create crypto client
        auto crypto_client = privacy_sandbox::bidding_auction_servers::CreateCryptoClient();
        
        // Decrypt the response using the crypto client
        auto decrypt_response = crypto_client->AeadDecrypt(decoded_ciphertext, decoded_secret);
        if (!decrypt_response.ok()) {
            last_error = "Failed to decrypt response: " + std::string(decrypt_response.status().message());
            result->error_message = last_error.c_str();
            return result;
        }
        
        std::string decrypted_payload = std::move(*decrypt_response->mutable_payload());
        
        // Parse the decrypted response into GetBidsRawResponse proto
        privacy_sandbox::bidding_auction_servers::GetBidsResponse::GetBidsRawResponse raw_response;
        if (!raw_response.ParseFromString(decrypted_payload)) {
            last_error = "Failed to parse GetBidsRawResponse proto from decrypted response";
            result->error_message = last_error.c_str();
            return result;
        }
        
        // Convert proto to JSON string
        std::string json_output;
        auto json_status = google::protobuf::util::MessageToJsonString(raw_response, &json_output);
        if (!json_status.ok()) {
            last_error = "Failed to convert proto to JSON: " + std::string(json_status.message());
            result->error_message = last_error.c_str();
            return result;
        }
        
        // Store the result
        last_response = json_output;
        result->success = 1;
        result->response = last_response.c_str();
        
    } catch (const std::exception& e) {
        last_error = std::string("Exception in decrypt: ") + e.what();
        result->error_message = last_error.c_str();
    } catch (...) {
        last_error = "Unknown error in decrypt";
        result->error_message = last_error.c_str();
    }
    
    return result;
}

} // extern "C"
