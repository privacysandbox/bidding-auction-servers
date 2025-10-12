#include <iostream>
#include <string>
#include <cstring>
#include <curl/curl.h>

extern "C" {
    typedef struct {
        int success;
        const char* response;
        const char* error_message;
    } SecureInvokeResult;

    int secure_invoke_init();
    void secure_invoke_cleanup();
    SecureInvokeResult* secure_invoke_encrypt(const char* input_json, const char* public_key, const char* key_id);
    SecureInvokeResult* secure_invoke_decrypt(const char* encrypted_response, const char* secret);
    void secure_invoke_free_result(SecureInvokeResult* result);
}

// Callback function to capture HTTP response
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

int main() {
    // Initialize the library
    if (!secure_invoke_init()) {
        std::cerr << "Failed to initialize secure_invoke library" << std::endl;
        return 1;
    }

    // Test data - use exact format from working example
    const char* input_json = R"({
    "client_type": "CLIENT_TYPE_BROWSER",
    "buyerInput": {
        "interestGroups": [
            {
                "name": "Rajni Kausalya",
                "biddingSignalsKeys": [
                    "9999999990"
                ],
                "userBiddingSignals": "{\"age\":29, \"average_amount_spent\":10000, \"total_spent\":20000}"
            }
        ]
    },
    "seller": "irctc.com",
    "publisherName": "irctc.com"
})";
    
    // Test public key (base64 encoded)
    const char* public_key = "87ey8XZPXAd+/+ytKv2GFUWW5j9zdepSJ2G4gebDwyM=";
    const char* key_id = "64";
    
    std::cout << "Testing encrypt function..." << std::endl;
    
    // Test encrypt
    SecureInvokeResult* encrypt_result = secure_invoke_encrypt(
        input_json, public_key, key_id);
    
    if (encrypt_result) {
        if (encrypt_result->success) {
            std::cout << "✓ Encryption successful!" << std::endl;
            std::cout << "Response length: " << strlen(encrypt_result->response) << std::endl;
            
            // Try to find the secret delimiter
            std::string encrypt_response(encrypt_result->response);
            size_t delimiter_pos = encrypt_response.find("|||SECRET|||");
            if (delimiter_pos != std::string::npos) {
                std::string encrypted_json = encrypt_response.substr(0, delimiter_pos);
                std::string secret = encrypt_response.substr(delimiter_pos + 12); // 12 is length of "|||SECRET|||"
                
                std::cout << "Encrypted JSON length: " << encrypted_json.length() << std::endl;
                std::cout << "Secret length: " << secret.length() << std::endl;
                std::cout << "First 200 chars of encrypted JSON: " << encrypted_json.substr(0, 200) << std::endl;
                std::cout << "Full encrypted JSON: " << encrypted_json << std::endl;
                
                // Send HTTP request to the server
                std::cout << "\nSending HTTP request to https://localhost:51052/v1/getbids..." << std::endl;
                
                CURL *curl;
                CURLcode res;
                std::string http_response;
                
                curl = curl_easy_init();
                if(curl) {
                    // Set the URL (try HTTP first)
                    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:51052/v1/getbids");
                    
                    // Set headers
                    struct curl_slist *headers = NULL;
                    headers = curl_slist_append(headers, "Content-Type: application/json");
                    headers = curl_slist_append(headers, "x-bna-client-ip: 127.0.0.1");
                    headers = curl_slist_append(headers, "x-user-agent: SecureInvoke-Test/1.0");
                    headers = curl_slist_append(headers, "x-accept-language: en-US");
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                    
                    // Set POST data
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, encrypted_json.c_str());
                    curl_easy_setopt(curl, CURLOPT_POST, 1L);
                    
                    // Set callback for response
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &http_response);
                    
                    // Skip SSL verification for localhost testing (insecure)
                    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
                    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYSTATUS, 0L);
                    
                    // Allow insecure connections
                    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
                    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                    
                    // Set timeout
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
                    
                    // Perform the request
                    res = curl_easy_perform(curl);
                    
                    if(res != CURLE_OK) {
                        std::cout << "✗ HTTP request failed: " << curl_easy_strerror(res) << std::endl;
                    } else {
                        long response_code;
                        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                        std::cout << "✓ HTTP request completed with status: " << response_code << std::endl;
                        std::cout << "HTTP response length: " << http_response.length() << std::endl;
                        std::cout << "HTTP response: " << http_response << std::endl;
                        
                        // Extract responseCiphertext for decryption
                        size_t cipher_pos = http_response.find("\"responseCiphertext\":");
                        if (cipher_pos != std::string::npos) {
                            size_t start_quote = http_response.find("\"", cipher_pos + 20);
                            size_t end_quote = http_response.find("\"", start_quote + 1);
                            if (start_quote != std::string::npos && end_quote != std::string::npos) {
                                std::string encrypted_response = http_response.substr(start_quote + 1, end_quote - start_quote - 1);
                                std::cout << "\nEncrypted response from server: " << encrypted_response.substr(0, 100) << "..." << std::endl;
                                std::cout << "Secret for decryption: " << secret.substr(0, 50) << "..." << std::endl;
                                
                                // Now decrypt the response
                                std::cout << "\nDecrypting server response..." << std::endl;
                                SecureInvokeResult* decrypt_result = secure_invoke_decrypt(
                                    encrypted_response.c_str(), secret.c_str());
                                
                                if (decrypt_result) {
                                    if (decrypt_result->success) {
                                        std::cout << "✓ Decryption successful!" << std::endl;
                                        std::cout << "Decrypted response: " << decrypt_result->response << std::endl;
                                    } else {
                                        std::cout << "✗ Decryption failed: " << (decrypt_result->error_message ? decrypt_result->error_message : "No error message") << std::endl;
                                    }
                                    secure_invoke_free_result(decrypt_result);
                                } else {
                                    std::cout << "✗ Decrypt function returned null" << std::endl;
                                }
                            }
                        }
                    }
                    
                    // Cleanup
                    curl_slist_free_all(headers);
                    curl_easy_cleanup(curl);
                } else {
                    std::cout << "✗ Failed to initialize CURL" << std::endl;
                }
            } else {
                std::cout << "⚠ Secret delimiter not found in response" << std::endl;
            }
        } else {
            std::cout << "✗ Encryption failed: " << (encrypt_result->error_message ? encrypt_result->error_message : "No error message") << std::endl;
        }
        secure_invoke_free_result(encrypt_result);
    } else {
        std::cout << "✗ Encrypt function returned null" << std::endl;
    }
    
    // Cleanup
    secure_invoke_cleanup();
    
    std::cout << "\nTest completed!" << std::endl;
    return 0;
}
