#!/usr/bin/env python3
"""
Python version of test_encrypt_http.cpp - End-to-end test for encrypt, HTTP request, and decrypt.

This script tests the complete bidding workflow:
1. Encrypt a GetBids request
2. Send encrypted request to bidding server via HTTP
3. Decrypt the server response
4. Display the final bidding results
"""

import json
import requests
import sys
import urllib3
from secure_invoke_crypto import BiddingCryptoClient, SecureInvokeCryptoError

# Disable SSL warnings for localhost testing
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


def create_test_bid_request():
    """
    Create a test bid request matching the C++ version.
    
    Returns:
        Dictionary representing GetBidsRawRequest
    """
    return {
        "client_type": "CLIENT_TYPE_BROWSER",
        "buyerInput": {
            "interestGroups": [
                {
                    "name": "Rajni Kausalya",
                    "biddingSignalsKeys": ["9999999990"],
                    "userBiddingSignals": "{\"age\":29, \"average_amount_spent\":10000, \"total_spent\":20000}"
                }
            ]
        },
        "seller": "irctc.com",
        "publisherName": "irctc.com"
    }


def send_http_request(encrypted_data: str, server_url: str = "http://localhost:51052/v1/getbids") -> dict:
    """
    Send encrypted request to the bidding server.
    
    Args:
        encrypted_data: JSON string containing the encrypted request
        server_url: URL of the bidding server
        
    Returns:
        Server response as dictionary
        
    Raises:
        requests.RequestException: If HTTP request fails
    """
    # Headers matching the C++ version
    headers = {
        "Content-Type": "application/json",
        "x-bna-client-ip": "127.0.0.1",
        "x-user-agent": "SecureInvoke-Test/1.0",
        "x-accept-language": "en-US"
    }
    
    print(f"Sending HTTP request to {server_url}...")
    print(f"Request payload length: {len(encrypted_data)} bytes")
    print(f"Request headers: {headers}")
    
    try:
        response = requests.post(
            server_url,
            data=encrypted_data,
            headers=headers,
            verify=False,  # Skip SSL verification for localhost
            timeout=10
        )
        
        print(f"✓ HTTP request completed with status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        print(f"Response length: {len(response.text)} bytes")
        
        response.raise_for_status()
        return response.json()
        
    except requests.exceptions.ConnectionError as e:
        print(f"✗ Connection error: {e}")
        raise
    except requests.exceptions.Timeout as e:
        print(f"✗ Timeout error: {e}")
        raise
    except requests.exceptions.HTTPError as e:
        print(f"✗ HTTP error: {e}")
        print(f"Response body: {response.text}")
        raise
    except requests.exceptions.RequestException as e:
        print(f"✗ Request error: {e}")
        raise


def extract_encrypted_response(server_response: dict) -> str:
    """
    Extract the encrypted response from server JSON.
    
    Args:
        server_response: Dictionary containing server response
        
    Returns:
        Base64 encoded encrypted response string
        
    Raises:
        ValueError: If responseCiphertext not found
    """
    if "responseCiphertext" not in server_response:
        available_keys = list(server_response.keys())
        raise ValueError(f"No 'responseCiphertext' found in server response. Available keys: {available_keys}")
    
    encrypted_response = server_response["responseCiphertext"]
    print(f"Extracted encrypted response: {len(encrypted_response)} bytes")
    print(f"Encrypted response (first 100 chars): {encrypted_response[:100]}...")
    
    return encrypted_response


def display_bid_results(decrypted_response: dict):
    """
    Display the decrypted bidding results in a readable format.
    
    Args:
        decrypted_response: Decrypted GetBidsResponse dictionary
    """
    print("\n" + "="*60)
    print("BIDDING RESULTS")
    print("="*60)
    
    if "bids" in decrypted_response and decrypted_response["bids"]:
        bids = decrypted_response["bids"]
        print(f"Total bids received: {len(bids)}")
        print()
        
        for i, bid in enumerate(bids, 1):
            print(f"Bid #{i}:")
            print(f"  Interest Group: {bid.get('interestGroupName', 'Unknown')}")
            print(f"  Bid Amount: ${bid.get('bid', 0)}")
            
            if 'ad' in bid:
                ad = bid['ad']
                print(f"  Ad URL: {ad.get('renderUrl', 'N/A')}")
                if 'metadata' in ad:
                    print(f"  Ad Metadata: {ad['metadata']}")
                if 'bidSignals' in ad:
                    print(f"  Bid Signals: {ad['bidSignals']}")
            
            if 'render' in bid:
                print(f"  Render URL: {bid['render']}")
            
            print()
    else:
        print("No bids received from the server.")
    
    if "updateInterestGroupList" in decrypted_response:
        update_list = decrypted_response["updateInterestGroupList"]
        if update_list:
            print(f"Interest group updates: {update_list}")
        else:
            print("No interest group updates.")
    
    print("="*60)


def main():
    """Main function to run the end-to-end test."""
    print("SecureInvoke End-to-End Test (Python)")
    print("="*50)
    
    # Configuration - matches the C++ test
    public_key = "87ey8XZPXAd+/+ytKv2GFUWW5j9zdepSJ2G4gebDwyM="
    key_id = "64"
    server_url = "http://localhost:51052/v1/getbids"
    
    try:
        # Step 1: Initialize crypto client
        print("1. Initializing crypto client...")
        client = BiddingCryptoClient(public_key, key_id)
        print(f"   Library version: {client.crypto.get_version()}")
        
        # Step 2: Create test bid request
        print("\n2. Creating test bid request...")
        bid_request = create_test_bid_request()
        print(f"   Created bid request for interest group: {bid_request['buyerInput']['interestGroups'][0]['name']}")
        print(f"   Bidding signals keys: {bid_request['buyerInput']['interestGroups'][0]['biddingSignalsKeys']}")
        
        # Step 3: Encrypt the request
        print("\n3. Encrypting bid request...")
        encryption_result = client.encrypt_bid_request(bid_request)
        print(f"   ✓ Encryption successful!")
        print(f"   Encrypted data length: {len(encryption_result.encrypted_data)} bytes")
        print(f"   Secret length: {len(encryption_result.secret)} bytes")
        print(f"   Encrypted data preview: {encryption_result.encrypted_data[:100]}...")
        
        # Step 4: Send HTTP request
        print(f"\n4. Sending request to server...")
        server_response = send_http_request(encryption_result.encrypted_data, server_url)
        print(f"   ✓ Server responded successfully")
        print(f"   Server response: {json.dumps(server_response, indent=2)}")
        
        # Step 5: Extract encrypted response
        print("\n5. Extracting encrypted response...")
        encrypted_response = extract_encrypted_response(server_response)
        print(f"   ✓ Extracted encrypted response")
        
        # Step 6: Decrypt the response
        print("\n6. Decrypting server response...")
        decrypted_response = client.decrypt_bid_response(encrypted_response, encryption_result.secret)
        print(f"   ✓ Decryption successful!")
        print(f"   Decrypted response: {json.dumps(decrypted_response, indent=2)}")
        
        # Step 7: Display results
        print("\n7. Processing results...")
        display_bid_results(decrypted_response)
        
        print("\n✓ End-to-end test completed successfully!")
        return True
        
    except SecureInvokeCryptoError as e:
        print(f"\n✗ Crypto error: {e}")
        return False
    except requests.RequestException as e:
        print(f"\n✗ HTTP request failed: {e}")
        print("   Make sure the bidding server is running at http://localhost:51052")
        return False
    except ValueError as e:
        print(f"\n✗ Data processing error: {e}")
        return False
    except Exception as e:
        print(f"\n✗ Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return False


def test_encryption_only():
    """
    Test only the encryption part (useful when server is not available).
    """
    print("\nSecureInvoke Encryption-Only Test")
    print("="*40)
    
    public_key = "87ey8XZPXAd+/+ytKv2GFUWW5j9zdepSJ2G4gebDwyM="
    key_id = "64"
    
    try:
        print("Initializing crypto client...")
        client = BiddingCryptoClient(public_key, key_id)
        
        print("Creating test bid request...")
        bid_request = create_test_bid_request()
        
        print("Encrypting bid request...")
        encryption_result = client.encrypt_bid_request(bid_request)
        
        print(f"✓ Encryption successful!")
        print(f"  Encrypted data: {len(encryption_result.encrypted_data)} bytes")
        print(f"  Secret: {len(encryption_result.secret)} bytes")
        print(f"  Ready to send to server: {encryption_result.encrypted_data[:50]}...")
        
        return True
        
    except Exception as e:
        print(f"✗ Encryption test failed: {e}")
        return False


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="SecureInvoke End-to-End Test")
    parser.add_argument("--encrypt-only", action="store_true", 
                       help="Test only encryption (skip HTTP request)")
    parser.add_argument("--server-url", default="http://localhost:51052/v1/getbids",
                       help="Bidding server URL")
    parser.add_argument("--public-key", default="87ey8XZPXAd+/+ytKv2GFUWW5j9zdepSJ2G4gebDwyM=",
                       help="Base64 encoded public key")
    parser.add_argument("--key-id", default="64",
                       help="Key ID")
    
    args = parser.parse_args()
    
    if args.encrypt_only:
        success = test_encryption_only()
    else:
        success = main()
    
    sys.exit(0 if success else 1)
