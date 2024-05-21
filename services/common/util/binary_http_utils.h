/*
 * Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_BINARY_HTTP_UTILS_H_
#define SERVICES_COMMON_UTIL_BINARY_HTTP_UTILS_H_

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "google/protobuf/util/json_util.h"
#include "quiche/binary_http/binary_http_message.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::protobuf::json::MessageToJsonString;
using ::google::protobuf::util::JsonStringToMessage;

inline constexpr char kContentType[] = "Content-Type";
inline constexpr char kJsonContentType[] = "application/json";
inline constexpr char kProtoContentType[] = "application/protobuf";

// Converts the incoming proto to Binary HTTP request.
// If to_json is set, the proto will be serialized to a JSON before encoding
// as binary HTTP, otherwise the proto is directly serialized into bytes string
// before binary HTTP encoding.
template <typename ProtoMessageType>
absl::StatusOr<std::string> ToBinaryHTTP(const ProtoMessageType& request_proto,
                                         bool to_json = true) {
  static_assert(
      std::is_base_of<google::protobuf::Message, ProtoMessageType>::value,
      "Request should be a google::protobuf::Message.");
  PS_VLOG(5) << "Converting request to binary HTTP ...";
  quiche::BinaryHttpRequest binary_http_request({});
  std::string serialized_request;
  if (to_json) {
    PS_RETURN_IF_ERROR(MessageToJsonString(request_proto, &serialized_request));
    PS_VLOG(6) << "JSON request: " << serialized_request;
    binary_http_request.AddHeaderField({kContentType, kJsonContentType});
  } else {
    serialized_request = request_proto.SerializeAsString();
    binary_http_request.AddHeaderField({kContentType, kProtoContentType});
  }
  binary_http_request.set_body(std::move(serialized_request));
  return binary_http_request.Serialize();
}

// Converts the incoming Binary HTTP response to the given proto type.
// If from_json is set, the data embedded in Binary HTTP response is expected
// to be a JSON, otherwise the encapsulated data is assumed to be bytes array
// representation of the proto.
template <typename ProtoMessageType>
absl::StatusOr<ProtoMessageType> FromBinaryHTTP(
    absl::string_view binary_http_response, bool from_json = true) {
  static_assert(
      std::is_base_of<google::protobuf::Message, ProtoMessageType>::value,
      "Response type should be a google::protobuf::Message.");
  PS_ASSIGN_OR_RETURN(auto retrieved_binary_http_response,
                      quiche::BinaryHttpResponse::Create(binary_http_response));
  std::string retrieved_binary_http_response_body;
  retrieved_binary_http_response.swap_body(retrieved_binary_http_response_body);
  ProtoMessageType response_proto;
  PS_VLOG(5) << "Converting the binary HTTP response to proto";
  if (from_json) {
    PS_RETURN_IF_ERROR(JsonStringToMessage(retrieved_binary_http_response_body,
                                           &response_proto));
  } else {
    if (!response_proto.ParseFromString(retrieved_binary_http_response_body)) {
      return absl::InvalidArgumentError(
          "Unable to convert the Binary HTTP response to proto");
    }
    PS_VLOG(2) << "Converted the http request to proto: "
               << response_proto.DebugString();
  }
  return response_proto;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_BINARY_HTTP_UTILS_H_
