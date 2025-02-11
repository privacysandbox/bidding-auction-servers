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

#ifndef TOOLS_TEST_PAYLOAD_GENERATOR_UTILS_H_
#define TOOLS_TEST_PAYLOAD_GENERATOR_UTILS_H_

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/common/util/hpke_utils.h"

// The methods in this util file can be used for packaging
// raw requests for testing. The packaging is done as follows -
// 1. Buyer Inputs are CBOR encoded and GZip compressed using
// PackageBuyerInputsForBrowser.
// 2. Output from PackageBuyerInputsForBrowser is included in a
// ProtectedAuctionInput object.
// 3. ProtectedAuctionInput is CBOR encoded and OHTTP Encrypted using
// PackagePayloadForBrowser. This produces a string which can be included in a
// SelectAdRequest object.

// The UnpackageBrowserAuctionResult method is used for unpackaging the
// response from the B&A server. The string response can be passed into the
// UnpackageBrowserAuctionResult method. The unpackaging is done as follows -
// 1. The auction_result_ciphertext is decrypted using the passed in encryption
//    context.
// 2. The decrypted string is decompressed using GZIP.
// 3. The decompressed string is CBOR decoded into an AuctionResult object.

// The methods use a default key for encryption/decryption which is used by the
// B&A servers in "test mode". For testing B&A servers not running in this
// mode, a public key hex string must be passed in for encryption/decryption.
namespace privacy_sandbox::bidding_auction_servers {

// Returns 1. Framed  + encoded (either CBOR or binary proto) and2. OHTTP
// encrypted (in that order) Protected Audience Payload string, and encryption
// context. The output payload from this function is meant to have a similar
// structure as an encrypted protected audience payload from a browser. This can
// be included in a SelectAd request for testing, similar to a payload obtained
// from the browser.
// The encryption context from this should be used for decrypting the
// response.
absl::StatusOr<std::pair<std::string, quiche::ObliviousHttpRequest::Context>>
PackagePayload(const ProtectedAuctionInput& protected_auction_input,
               ClientType client_type, const HpkeKeyset& keyset);

// Returns 1. CBOR Encoded and 2. GZIP Compressed (in that order)
// Map of IG owners -> Buyer Inputs. The output from this method
// can be used to populate the buyer_input map in ProtectedAudienceInput
// for creating requests from a browser source.
absl::StatusOr<google::protobuf::Map<std::string, std::string>>
PackageBuyerInputsForBrowser(
    const google::protobuf::Map<std::string, BuyerInputForBidding>&
        buyer_inputs);

// Returns a map of IG owners -> GZIP compressed proto binary Buyer Inputs.
// The output from this method can be used to populate the buyer_input map in
// ProtectedAuctionInput for mimicking requests from android.
absl::StatusOr<google::protobuf::Map<std::string, std::string>>
PackageBuyerInputsForApp(
    const google::protobuf::Map<std::string, BuyerInputForBidding>&
        buyer_inputs);

// Returns a decrypted, decompressed and decoded proto response
// for response strings generated for a browser or android source.
// The encryption context from PackagePayload needs
// to be provided for decrypting the response.
// Note: This method consumes `auction_result_ciphertext` input and hence should
// not be used by the caller afterwards.
absl::StatusOr<std::pair<AuctionResult, std::string>>
UnpackageAuctionResultAndNonce(
    std::string& auction_result_ciphertext, ClientType client_type,
    quiche::ObliviousHttpRequest::Context& oblivious_request_context,
    const HpkeKeyset& keyset);

// Returns an encoded, compressed and encrypted auction result
// for a server component auction.
absl::StatusOr<HpkeMessage> PackageServerComponentAuctionResult(
    const AuctionResult& auction_result, const HpkeKeyset& keyset);

// Returns a decrypted, decompressed and decoded proto response
// for response strings generated for a server component auction.
absl::StatusOr<AuctionResult> UnpackageResultForServerComponentAuction(
    absl::string_view ciphertext, absl::string_view key_id,
    const HpkeKeyset& keyset);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // TOOLS_TEST_PAYLOAD_GENERATOR_UTILS_H_
