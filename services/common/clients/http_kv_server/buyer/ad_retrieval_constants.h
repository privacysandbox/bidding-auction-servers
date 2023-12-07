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

#ifndef SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_BUYER_AD_RETRIEVAL_CONSTANTS_H_
#define SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_BUYER_AD_RETRIEVAL_CONSTANTS_H_

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kTags[] = "tags";
inline constexpr char kData[] = "data";
inline constexpr char kKeyList[] = "keyList";
inline constexpr char kKeyValues[] = "keyValues";
inline constexpr char kProtectedSignals[] = "protectedSignals";
inline constexpr char kContextualSignals[] = "contextualSignals";
inline constexpr char kRetrievalData[] = "protectedEmbeddings";
inline constexpr char kAcceptLanguage[] = "X-Accept-Language";
inline constexpr char kUserAgent[] = "X-User-Agent";
inline constexpr char kClientIp[] = "X-BnA-Client-IP";
inline constexpr char kDeviceMetadata[] = "deviceMetadata";
inline constexpr char kPartitions[] = "partitions";
inline constexpr char kKeyGroupOutputs[] = "keyGroupOutputs";
inline constexpr char kArguments[] = "arguments";
inline constexpr int kPreWarmRequestTimeout = 60000;
inline constexpr char kKeyValDelimiter[] = ": ";
inline constexpr char kApplicationJsonHeader[] =
    "Content-Type: application/json";
inline constexpr char kContextualEmbeddings[] = "contextualEmbeddings";

inline constexpr char kUnexpectedPartitions[] =
    "Unexpected number of partitions found.";
inline constexpr char kUnexpectedTags[] = "Unexpected number of tags found.";

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_KV_SERVER_BUYER_AD_RETRIEVAL_CONSTANTS_H_
