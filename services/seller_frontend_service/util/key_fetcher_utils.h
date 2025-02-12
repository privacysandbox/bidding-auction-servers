/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_KEY_FETCHER_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_KEY_FETCHER_UTILS_H_

#include <memory>
#include <string>
#include <vector>

#include "api/bidding_auction_servers.pb.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/seller_frontend_service/util/config_param_parser.h"
#include "src/encryption/key_fetcher/interface/public_key_fetcher_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kInvalidTypeForCloudPlatform =
    "Invalid type for cloud platform (expected string)";
inline constexpr absl::string_view kEmptyCloudPlatformError =
    "Empty value for cloud platform";
inline constexpr absl::string_view kUnsupportedCloudPlatformValue =
    "Unsupported platform type: ";
inline constexpr absl::string_view kInvalidTypeForEndpoint =
    "Invalid type for Public Key Service endpoint: (expected string)";
inline constexpr absl::string_view kEmptyEndpointError =
    "Empty value for Public Key Service endpoint";

using PlatformToPublicKeyServiceEndpointMap = absl::flat_hash_map<
    server_common::CloudPlatform,
    std::vector<google::scp::cpio::PublicKeyVendingServiceEndpoint>>;

// Parses stringified JSON map containing entries of supported cloud platforms
// to Public Key Service endpoints. This method is called within
// CreateSfePublicKeyFetcher() below; the JSON parsing logic is separated out
// to make UT'ing easier.
absl::StatusOr<PlatformToPublicKeyServiceEndpointMap>
ParseCloudPlatformPublicKeysMap(absl::string_view public_keys_endpoint_map_str);

// Validates the cloud platform of every buyer is present in the SFE's public
// key endpoint map.
absl::Status ValidateSfePublicKeyEndpoints(
    const PlatformToPublicKeyServiceEndpointMap& endpoints_map,
    const absl::flat_hash_map<std::string, BuyerServiceEndpoint>&
        buyer_server_hosts);

// Creates an instance of PublicKeyFetcherInterface.
absl::StatusOr<std::unique_ptr<server_common::PublicKeyFetcherInterface>>
CreateSfePublicKeyFetcher(
    const TrustedServersConfigClient& config_client,
    const absl::flat_hash_map<std::string, BuyerServiceEndpoint>&
        buyer_server_hosts);

server_common::CloudPlatform ProtoCloudPlatformToScpCloudPlatform(
    EncryptionCloudPlatform cloud_platform);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_KEY_FETCHER_UTILS_H_
