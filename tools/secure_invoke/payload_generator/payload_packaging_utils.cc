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

#include "tools/secure_invoke/payload_generator/payload_packaging_utils.h"

#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/compression/gzip.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/cpp/communication/encoding_utils.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::pair<std::string, quiche::ObliviousHttpRequest::Context>>
PackagePayload(const ProtectedAuctionInput& protected_auction_input,
               ClientType client_type, absl::string_view public_key,
               uint8_t key_id) {
  // Encode request.
  std::string encoded_request;
  switch (client_type) {
    case CLIENT_TYPE_BROWSER: {
      PS_ASSIGN_OR_RETURN(
          auto serialized_request,
          CborEncodeProtectedAuctionProto(protected_auction_input));
      PS_ASSIGN_OR_RETURN(
          encoded_request,
          server_common::EncodeResponsePayload(
              server_common::CompressionType::kGzip, serialized_request,
              GetEncodedDataSize(serialized_request.size())));
    } break;
    case CLIENT_TYPE_ANDROID: {
      // Serialize the protected audience input and frame it.
      std::string serialized_request =
          protected_auction_input.SerializeAsString();
      PS_ASSIGN_OR_RETURN(
          encoded_request,
          server_common::EncodeResponsePayload(
              server_common::CompressionType::kGzip, serialized_request,
              GetEncodedDataSize(serialized_request.size())));
    } break;
    default:
      break;
  }

  // Encrypt request.
  PS_ASSIGN_OR_RETURN((quiche::ObliviousHttpRequest ohttp_request),
                      CreateValidEncryptedRequest(std::move(encoded_request),
                                                  public_key, key_id));
  std::string encrypted_request = ohttp_request.EncapsulateAndSerialize();
  auto context = std::move(ohttp_request).ReleaseContext();
  return absl::StatusOr<
      std::pair<std::string, quiche::ObliviousHttpRequest::Context>>(
      {std::move(encrypted_request), std::move(context)});
}

absl::StatusOr<google::protobuf::Map<std::string, std::string>>
PackageBuyerInputsForApp(
    const google::protobuf::Map<std::string, BuyerInput>& buyer_inputs) {
  return GetProtoEncodedBuyerInputs(buyer_inputs);
}

absl::StatusOr<google::protobuf::Map<std::string, std::string>>
PackageBuyerInputsForBrowser(
    const google::protobuf::Map<std::string, BuyerInput>& buyer_inputs) {
  return GetEncodedBuyerInputMap(buyer_inputs);
}

absl::StatusOr<AuctionResult> UnpackageAuctionResult(
    absl::string_view auction_result_ciphertext, ClientType client_type,
    quiche::ObliviousHttpRequest::Context& context,
    absl::string_view public_key, uint8_t key_id) {
  // Decrypt Response.
  PS_ASSIGN_OR_RETURN((quiche::ObliviousHttpResponse decrypted_response),
                      DecryptEncapsulatedResponse(auction_result_ciphertext,
                                                  context, public_key, key_id));

  switch (client_type) {
    case CLIENT_TYPE_BROWSER: {
      // Decompress the encoded response.
      PS_ASSIGN_OR_RETURN((std::string decompressed_response),
                          UnframeAndDecompressAuctionResult(
                              decrypted_response.GetPlaintextData()));

      // Decode the response.
      return CborDecodeAuctionResultToProto(decompressed_response);
    }
    case CLIENT_TYPE_ANDROID: {
      absl::StatusOr<server_common::DecodedRequest> decoded_response;
      PS_ASSIGN_OR_RETURN((std::string decompressed_response),
                          UnframeAndDecompressAuctionResult(
                              decrypted_response.GetPlaintextData()));
      AuctionResult auction_result;
      if (!auction_result.ParseFromArray(decompressed_response.data(),
                                         decompressed_response.size())) {
        return absl::InternalError(
            "Unable to decode the response (CLIENT_TYPE_ANDROID) from server");
      }
      return auction_result;
    } break;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unknown client type: ", client_type));
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
