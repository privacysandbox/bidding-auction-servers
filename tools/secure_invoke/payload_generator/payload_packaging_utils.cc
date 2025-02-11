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

#include <memory>

#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/compression/gzip.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/util/oblivious_http_utils.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/proto_mapping_util.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/encryption/key_fetcher/fake_key_fetcher_manager.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::pair<std::string, quiche::ObliviousHttpRequest::Context>>
PackagePayload(const ProtectedAuctionInput& protected_auction_input,
               ClientType client_type, const HpkeKeyset& keyset) {
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
                      CreateValidEncryptedRequest(encoded_request, keyset));
  // Prepend with a zero byte to follow the new B&A request format that uses
  // custom media types for request encryption/request decryption.
  std::string encrypted_request =
      '\0' + ohttp_request.EncapsulateAndSerialize();
  auto context = std::move(ohttp_request).ReleaseContext();
  return absl::StatusOr<
      std::pair<std::string, quiche::ObliviousHttpRequest::Context>>(
      {std::move(encrypted_request), std::move(context)});
}

absl::StatusOr<google::protobuf::Map<std::string, std::string>>
PackageBuyerInputsForApp(
    const google::protobuf::Map<std::string, BuyerInputForBidding>&
        buyer_inputs) {
  return GetProtoEncodedBuyerInputs(buyer_inputs);
}

absl::StatusOr<google::protobuf::Map<std::string, std::string>>
PackageBuyerInputsForBrowser(
    const google::protobuf::Map<std::string, BuyerInputForBidding>&
        buyer_inputs) {
  return GetEncodedBuyerInputMap(buyer_inputs);
}

absl::StatusOr<std::pair<AuctionResult, std::string>>
UnpackageAuctionResultAndNonce(std::string& auction_result_ciphertext,
                               ClientType client_type,
                               quiche::ObliviousHttpRequest::Context& context,
                               const HpkeKeyset& keyset) {
  // Decrypt Response.
  PS_ASSIGN_OR_RETURN(
      auto decrypted_response,
      FromObliviousHTTPResponse(auction_result_ciphertext, context,
                                kBiddingAuctionOhttpResponseLabel));

  switch (client_type) {
    case CLIENT_TYPE_BROWSER: {
      // Decompress the encoded response.
      PS_ASSIGN_OR_RETURN(
          (std::string decompressed_response),
          UnframeAndDecompressAuctionResult(decrypted_response));

      // Decode the response.
      return CborDecodeAuctionResultAndNonceToProto(decompressed_response);
    }
    case CLIENT_TYPE_ANDROID: {
      absl::StatusOr<server_common::DecodedRequest> decoded_response;
      PS_ASSIGN_OR_RETURN(
          (std::string decompressed_response),
          UnframeAndDecompressAuctionResult(decrypted_response));
      AuctionResult auction_result;
      if (!auction_result.ParseFromArray(decompressed_response.data(),
                                         decompressed_response.size())) {
        return absl::InternalError(
            "Unable to decode the response (CLIENT_TYPE_ANDROID) from server");
      }
      return absl::StatusOr<std::pair<AuctionResult, std::string>>(
          {auction_result, ""});
    } break;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unknown client type: ", client_type));
  }
}

absl::StatusOr<HpkeMessage> PackageServerComponentAuctionResult(
    const AuctionResult& auction_result, const HpkeKeyset& keyset) {
  // Serialize the protected audience input and frame it.
  std::string serialized_result = auction_result.SerializeAsString();

  PS_ASSIGN_OR_RETURN(std::string compressed_data,
                      GzipCompress(serialized_result));
  PS_ASSIGN_OR_RETURN(
      std::string plaintext_response,
      server_common::EncodeResponsePayload(
          server_common::CompressionType::kGzip, compressed_data,
          GetEncodedDataSize(compressed_data.size())));
  auto key_fetcher_manager =
      std::make_unique<server_common::FakeKeyFetcherManager>(
          keyset.public_key, keyset.private_key, std::to_string(keyset.key_id));
  auto crypto_client = CreateCryptoClient();
  return HpkeEncrypt(plaintext_response, *crypto_client, *key_fetcher_manager,
                     server_common::CloudPlatform::kGcp);
}

absl::StatusOr<AuctionResult> UnpackageResultForServerComponentAuction(
    absl::string_view ciphertext, absl::string_view key_id,
    const HpkeKeyset& keyset) {
  auto key_fetcher_manager =
      std::make_unique<server_common::FakeKeyFetcherManager>(
          keyset.public_key, keyset.private_key, absl::StrCat(key_id));
  auto crypto_client = CreateCryptoClient();
  SelectAdRequest::ComponentAuctionResult car;
  car.set_key_id(key_id);
  car.set_auction_result_ciphertext(ciphertext);
  return UnpackageServerAuctionComponentResult(car, *crypto_client,
                                               *key_fetcher_manager);
}

}  // namespace privacy_sandbox::bidding_auction_servers
