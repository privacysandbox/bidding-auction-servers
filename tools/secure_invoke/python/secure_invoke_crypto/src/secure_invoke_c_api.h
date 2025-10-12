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

#ifndef TOOLS_SECURE_INVOKE_C_API_H_
#define TOOLS_SECURE_INVOKE_C_API_H_

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: SecureInvokeConfig removed - not needed for crypto-only API

// C-compatible result structure
typedef struct {
    int success;
    const char* response;
    const char* error_message;
} SecureInvokeResult;

// Initialize the secure invoke library
int secure_invoke_init();

// Cleanup the secure invoke library
void secure_invoke_cleanup();

// NOTE: secure_invoke_execute removed - not needed for crypto-only API

// Free the result structure
void secure_invoke_free_result(SecureInvokeResult* result);

// Get version information
const char* secure_invoke_get_version();

// Encrypt a GetBids request - returns encrypted JSON string
// input_json: JSON string of GetBidsRawRequest
// public_key: Base64 encoded public key
// key_id: Key ID as string
// Returns pointer to result that must be freed with secure_invoke_free_result
SecureInvokeResult* secure_invoke_encrypt(
    const char* input_json,
    const char* public_key,
    const char* key_id
);

// Decrypt a response - returns decrypted JSON string
// encrypted_response: Base64 encoded encrypted response
// secret: Secret string from encryption step
// Returns pointer to result that must be freed with secure_invoke_free_result
SecureInvokeResult* secure_invoke_decrypt(
    const char* encrypted_response,
    const char* secret
);

#ifdef __cplusplus
}
#endif

#endif  // TOOLS_SECURE_INVOKE_C_API_H_
