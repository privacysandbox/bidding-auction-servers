#!/usr/bin/env python3
"""
Python crypto API for secure_invoke C++ library.

This module provides clean Python APIs for the encrypt and decrypt functions
from the secure_invoke C++ library, designed for programmatic use in other Python code.
"""

import ctypes
import os
import sys
import json
from ctypes import Structure, c_char_p, c_int, POINTER
from typing import Dict, Optional, Any, Union, Tuple, NamedTuple
import base64


class SecureInvokeResult(Structure):
    """C-compatible result structure"""
    _fields_ = [
        ("success", c_int),
        ("response", c_char_p),
        ("error_message", c_char_p),
    ]


class EncryptionResult(NamedTuple):
    """Result from encryption operation"""
    encrypted_data: str
    secret: str
    

class SecureInvokeCryptoError(Exception):
    """Custom exception for secure_invoke crypto errors"""
    pass


class SecureInvokeCrypto:
    """
    Python crypto interface for the secure_invoke C++ library.
    
    This class provides clean encrypt and decrypt APIs for programmatic use.
    """
    
    def __init__(self, library_path: Optional[str] = None):
        """
        Initialize the SecureInvokeCrypto.
        
        Args:
            library_path: Path to the shared library. If None, attempts to find it automatically.
        """
        self._lib = None
        self._load_library(library_path)
        self._setup_function_signatures()
        
        # Initialize the library
        if not self._lib.secure_invoke_init():
            raise SecureInvokeCryptoError("Failed to initialize secure_invoke library")
    
    def __del__(self):
        """Cleanup when the object is destroyed"""
        if self._lib:
            self._lib.secure_invoke_cleanup()
    
    def _load_library(self, library_path: Optional[str]):
        """Load the shared library"""
        if library_path is None:
            # Get the package directory
            package_dir = os.path.dirname(os.path.abspath(__file__))
            
            # Try to find the library in common locations
            possible_paths = [
                os.path.join(package_dir, "lib", "libsecure_invoke.so"),  # Package lib directory
                "./libsecure_invoke.so",
                "../bazel-bin/tools/secure_invoke/libsecure_invoke.so",
                "/usr/local/lib/libsecure_invoke.so",
                "/usr/lib/libsecure_invoke.so",
            ]
            
            for path in possible_paths:
                if os.path.exists(path):
                    library_path = path
                    break
            
            if library_path is None:
                raise SecureInvokeCryptoError(
                    "Could not find secure_invoke shared library. "
                    "Please provide the library_path parameter."
                )
        
        try:
            self._lib = ctypes.CDLL(library_path)
        except OSError as e:
            raise SecureInvokeCryptoError(f"Failed to load library {library_path}: {e}")
    
    def _setup_function_signatures(self):
        """Setup function signatures for proper ctypes interfacing"""
        # secure_invoke_init
        self._lib.secure_invoke_init.argtypes = []
        self._lib.secure_invoke_init.restype = c_int
        
        # secure_invoke_cleanup
        self._lib.secure_invoke_cleanup.argtypes = []
        self._lib.secure_invoke_cleanup.restype = None
        
        # secure_invoke_encrypt
        self._lib.secure_invoke_encrypt.argtypes = [c_char_p, c_char_p, c_char_p]
        self._lib.secure_invoke_encrypt.restype = POINTER(SecureInvokeResult)
        
        # secure_invoke_decrypt
        self._lib.secure_invoke_decrypt.argtypes = [c_char_p, c_char_p]
        self._lib.secure_invoke_decrypt.restype = POINTER(SecureInvokeResult)
        
        # secure_invoke_free_result
        self._lib.secure_invoke_free_result.argtypes = [POINTER(SecureInvokeResult)]
        self._lib.secure_invoke_free_result.restype = None
        
        # secure_invoke_get_version
        self._lib.secure_invoke_get_version.argtypes = []
        self._lib.secure_invoke_get_version.restype = c_char_p
    
    def get_version(self) -> str:
        """Get the library version"""
        version = self._lib.secure_invoke_get_version()
        return version.decode('utf-8') if version else "unknown"
    
    def encrypt(self, 
                input_json: Union[Dict, str],
                public_key: str,
                key_id: str) -> EncryptionResult:
        """
        Encrypt a GetBids request.
        
        Args:
            input_json: Either a dictionary representing GetBidsRawRequest or JSON string
            public_key: Base64 encoded public key
            key_id: Key ID as string
            
        Returns:
            EncryptionResult containing encrypted_data and secret
            
        Raises:
            SecureInvokeCryptoError: If the encryption operation fails
        """
        # Convert input to JSON string if it's a dictionary
        if isinstance(input_json, dict):
            json_str = json.dumps(input_json)
        elif isinstance(input_json, str):
            # Validate that it's valid JSON
            try:
                json.loads(input_json)
                json_str = input_json
            except json.JSONDecodeError as e:
                raise SecureInvokeCryptoError(f"Invalid JSON string: {e}")
        else:
            raise SecureInvokeCryptoError(f"input_json must be dict or JSON string, got {type(input_json)}")
        
        # Call the C++ encrypt function
        result_ptr = self._lib.secure_invoke_encrypt(
            json_str.encode('utf-8'),
            public_key.encode('utf-8'),
            key_id.encode('utf-8')
        )
        
        if not result_ptr:
            raise SecureInvokeCryptoError("secure_invoke_encrypt returned null")
        
        try:
            result = result_ptr.contents
            
            if not result.success:
                error_msg = result.error_message.decode('utf-8') if result.error_message else "Unknown error"
                raise SecureInvokeCryptoError(f"Encryption failed: {error_msg}")
            
            if not result.response:
                raise SecureInvokeCryptoError("Encryption succeeded but no response data")
            
            # Parse the response to extract encrypted data and secret
            response_str = result.response.decode('utf-8')
            
            # Look for the secret delimiter
            delimiter = "|||SECRET|||"
            delimiter_pos = response_str.find(delimiter)
            if delimiter_pos == -1:
                raise SecureInvokeCryptoError("Secret delimiter not found in encryption response")
            
            encrypted_data = response_str[:delimiter_pos]
            secret = response_str[delimiter_pos + len(delimiter):]
            
            return EncryptionResult(encrypted_data=encrypted_data, secret=secret)
        
        finally:
            # Free the result
            self._lib.secure_invoke_free_result(result_ptr)
    
    def decrypt(self, 
                encrypted_response: str,
                secret: str) -> Dict[str, Any]:
        """
        Decrypt a server response.
        
        Args:
            encrypted_response: Base64 encoded encrypted response from server
            secret: Secret string from the encryption step
            
        Returns:
            Decrypted response as a dictionary
            
        Raises:
            SecureInvokeCryptoError: If the decryption operation fails
        """
        # Call the C++ decrypt function
        result_ptr = self._lib.secure_invoke_decrypt(
            encrypted_response.encode('utf-8'),
            secret.encode('utf-8')
        )
        
        if not result_ptr:
            raise SecureInvokeCryptoError("secure_invoke_decrypt returned null")
        
        try:
            result = result_ptr.contents
            
            if not result.success:
                error_msg = result.error_message.decode('utf-8') if result.error_message else "Unknown error"
                raise SecureInvokeCryptoError(f"Decryption failed: {error_msg}")
            
            if not result.response:
                raise SecureInvokeCryptoError("Decryption succeeded but no response data")
            
            # Parse the JSON response
            response_str = result.response.decode('utf-8')
            try:
                return json.loads(response_str)
            except json.JSONDecodeError as e:
                raise SecureInvokeCryptoError(f"Failed to parse decrypted response as JSON: {e}")
        
        finally:
            # Free the result
            self._lib.secure_invoke_free_result(result_ptr)


class BiddingCryptoClient:
    """
    High-level client for bidding auction cryptographic operations.
    
    This class provides convenient methods for common bidding operations.
    """
    
    def __init__(self, 
                 public_key: str, 
                 key_id: str,
                 library_path: Optional[str] = None):
        """
        Initialize the bidding crypto client.
        
        Args:
            public_key: Base64 encoded public key for encryption
            key_id: Key ID for the public key
            library_path: Path to the shared library (optional)
        """
        self.crypto = SecureInvokeCrypto(library_path)
        self.public_key = public_key
        self.key_id = key_id
    
    def encrypt_bid_request(self, bid_request: Dict[str, Any]) -> EncryptionResult:
        """
        Encrypt a bid request.
        
        Args:
            bid_request: Dictionary representing the GetBidsRawRequest
            
        Returns:
            EncryptionResult with encrypted data and secret
        """
        return self.crypto.encrypt(bid_request, self.public_key, self.key_id)
    
    def decrypt_bid_response(self, encrypted_response: str, secret: str) -> Dict[str, Any]:
        """
        Decrypt a bid response from the server.
        
        Args:
            encrypted_response: Encrypted response from the bidding server
            secret: Secret from the encryption operation
            
        Returns:
            Decrypted GetBidsResponse as a dictionary
        """
        return self.crypto.decrypt(encrypted_response, secret)
    
    def create_sample_bid_request(self) -> Dict[str, Any]:
        """
        Create a sample bid request for testing.
        
        Returns:
            Dictionary representing a sample GetBidsRawRequest
        """
        return {
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
        }


def demo():
    """
    Demonstration of the crypto APIs.
    """
    # Default test keys (same as in the test files)
    public_key = "87ey8XZPXAd+/+ytKv2GFUWW5j9zdepSJ2G4gebDwyM="
    key_id = "64"
    
    try:
        print("SecureInvoke Crypto Demo")
        print("=" * 40)
        
        # Initialize the crypto client
        print("Initializing crypto client...")
        client = BiddingCryptoClient(public_key, key_id)
        print(f"Library version: {client.crypto.get_version()}")
        
        # Create a sample bid request
        print("\nCreating sample bid request...")
        bid_request = client.create_sample_bid_request()
        print(f"Bid request: {json.dumps(bid_request, indent=2)}")
        
        # Encrypt the request
        print("\nEncrypting bid request...")
        encryption_result = client.encrypt_bid_request(bid_request)
        print(f"Encrypted data length: {len(encryption_result.encrypted_data)} bytes")
        print(f"Secret length: {len(encryption_result.secret)} bytes")
        print(f"Encrypted data (first 100 chars): {encryption_result.encrypted_data[:100]}...")
        print(f"Secret (first 50 chars): {encryption_result.secret[:50]}...")
        
        # For demo purposes, we'll simulate a server response
        # In real usage, you would send the encrypted_data to the server and get back a response
        print("\nDemo completed successfully!")
        print("Next steps: Send encrypted_data to bidding server, then use decrypt_bid_response() on the response")
        
    except SecureInvokeCryptoError as e:
        print(f"Crypto error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    demo()
