"""
SecureInvoke Python Package

This package provides Python bindings for the SecureInvoke cryptographic library
used in Privacy Sandbox bidding and auction systems.

Main classes:
- SecureInvokeCrypto: Low-level crypto interface
- BiddingCryptoClient: High-level client for bidding operations
- EncryptionResult: Result object containing encrypted data and secret

Example usage:
    from secure_invoke import BiddingCryptoClient
    
    client = BiddingCryptoClient(public_key, key_id)
    result = client.encrypt_bid_request(bid_data)
    # Send result.encrypted_data to server
    response = client.decrypt_bid_response(server_response, result.secret)
"""

from ._version import __version__
from .crypto import (
    SecureInvokeCrypto,
    BiddingCryptoClient,
    EncryptionResult,
    SecureInvokeCryptoError,
)

__all__ = [
    "__version__",
    "SecureInvokeCrypto",
    "BiddingCryptoClient", 
    "EncryptionResult",
    "SecureInvokeCryptoError",
]

# Package metadata
__author__ = "Privacy Sandbox Team"
__email__ = "privacy-sandbox@example.com"
__description__ = "Python bindings for SecureInvoke cryptographic library"
