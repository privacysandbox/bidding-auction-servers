// Copyright 2024 Google LLC
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

#include "services/common/clients/kv_server/kv_async_client.h"

#include <grpcpp/grpcpp.h>

#include "include/grpcpp/support/status_code_enum.h"
#include "services/common/clients/async_grpc/default_async_grpc_client.h"
#include "services/common/util/client_context_util.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/public/cpio/interface/crypto_client/crypto_client_interface.h"
#include "src/public/cpio/proto/public_key_service/v1/public_key_service.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr std::string_view kKVContentTypeHeader = "kv-content-type";
inline constexpr std::string_view kContentEncodingProtoHeaderValue =
    "message/ad-auction-trusted-signals-request+proto";
// TODO: once the KV dependency is bumped, these constants are available and
// should be reused.
inline constexpr absl::string_view kKVOhttpRequestLabel =
    "message/ad-auction-trusted-signals-request";
inline constexpr absl::string_view kKVOhttpResponseLabel =
    "message/ad-auction-trusted-signals-response";

using ::google::cmrt::sdk::public_key_service::v1::PublicKey;

KVAsyncGrpcClient::KVAsyncGrpcClient(
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    std::unique_ptr<kv_server::v2::KeyValueService::Stub> stub)
    : AsyncClient<ObliviousGetValuesRequest, google::api::HttpBody,
                  GetValuesRequest, GetValuesResponse>(),
      key_fetcher_manager_(*key_fetcher_manager),
      stub_(std::move(stub)) {
#if defined(CLOUD_PLATFORM_AWS)
  cloud_platform_ = server_common::CloudPlatform::kAws;
#elif defined(CLOUD_PLATFORM_GCP)
  cloud_platform_ = server_common::CloudPlatform::kGcp;
#else
  cloud_platform_ = server_common::CloudPlatform::kLocal;
#endif
}

void KVAsyncGrpcClient::SendRpc(
    ObliviousHttpRequestUptr oblivious_http_context,
    grpc::ClientContext* context,
    RawClientParams<ObliviousGetValuesRequest, google::api::HttpBody,
                    GetValuesResponse>* params) const {
  PS_VLOG(5) << "KVAsyncGrpcClient SendRpc invoked ...";
  stub_->async()->ObliviousGetValues(
      context, params->RequestRef(), params->ResponseRef(),
      [captured_oblivious_http_context = oblivious_http_context.release(),
       params](const grpc::Status& status) {
        auto oblivious_http_request_uptr =
            ObliviousHttpRequestUptr(captured_oblivious_http_context);
        if (!status.ok()) {
          PS_LOG(ERROR) << "SendRPC completion status not ok: "
                        << server_common::ToAbslStatus(status);
          params->OnDone(status);
          return;
        }
        PS_VLOG(6) << "SendRPC completion status ok";
        auto plain_text_http_response = FromObliviousHTTPResponse(
            *params->ResponseRef()->mutable_data(),
            *captured_oblivious_http_context, kKVOhttpResponseLabel);
        if (!plain_text_http_response.ok()) {
          // Passing an incorrect label will result in "SslErrorAsStatus("Failed
          // to export secret.")" see
          // https://github.com/google/quiche/blob/6fe69b2cf77d5fc175a729bc7a6c322a6388b8b6/quiche/oblivious_http/buffers/oblivious_http_response.cc#L265
          PS_LOG(ERROR, SystemLogContext())
              << "KVAsyncGrpcClient failed to get HTTP response";
          params->OnDone(
              grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           plain_text_http_response.status().ToString()));
          return;
        }
        auto deframed_req =
            privacy_sandbox::server_common::DecodeRequestPayload(
                *plain_text_http_response);
        if (!deframed_req.ok()) {
          PS_LOG(ERROR, SystemLogContext())
              << "Unpadding response failed: " << deframed_req.status();
          params->OnDone(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                      deframed_req.status().ToString()));
          return;
        }
        GetValuesResponse response;
        if (!response.ParseFromString(deframed_req->compressed_data)) {
          PS_LOG(ERROR, SystemLogContext())
              << "Unable to convert the response to proto";
          params->OnDone(
              grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                           "Unable to convert the response to proto"));
          return;
        }
        PS_VLOG(7) << "Retrieved proto response: " << response.DebugString();
        params->SetRawResponse(
            std::make_unique<GetValuesResponse>(std::move(response)));
        PS_VLOG(6) << "Returning the decrypted response via callback";
        params->OnDone(status);
      });
}

absl::Status KVAsyncGrpcClient::ExecuteInternal(
    std::unique_ptr<GetValuesRequest> raw_request, grpc::ClientContext* context,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                            ResponseMetadata) &&>
        on_done,
    absl::Duration timeout, RequestConfig request_config) {
  PS_VLOG(6) << "Raw request:\n" << raw_request->DebugString();
  std::string serialized_req = raw_request->SerializeAsString();

  PS_VLOG(5) << "Fetching public Key ...";
  PS_ASSIGN_OR_RETURN(auto public_key,
                      key_fetcher_manager_.GetPublicKey(cloud_platform_));
  std::string unescaped_public_key_bytes;
  if (!absl::Base64Unescape(public_key.public_key(),
                            &unescaped_public_key_bytes)) {
    return absl::InternalError(
        absl::StrCat("Failed to base64 decode the fetched public key: ",
                     public_key.public_key()));
  }
  auto encoded_data_size = GetEncodedDataSize(serialized_req.size());
  auto maybe_padded_request =
      privacy_sandbox::server_common::EncodeResponsePayload(
          privacy_sandbox::server_common::CompressionType::kUncompressed,
          std::move(serialized_req), encoded_data_size);
  if (!maybe_padded_request.ok()) {
    PS_LOG(ERROR, SystemLogContext())
        << "Padding failed: " << maybe_padded_request.status().message();
    return maybe_padded_request.status();
  }
  PS_ASSIGN_OR_RETURN(
      auto oblivious_http_request,
      ToObliviousHTTPRequest(*maybe_padded_request, unescaped_public_key_bytes,
                             stoi(public_key.key_id()),
                             kv_server::kKEMParameter, kv_server::kKDFParameter,
                             kv_server::kAEADParameter, kKVOhttpRequestLabel));
  PS_VLOG(6) << "Encapsulating and serializing request";
  std::string encrypted_request =
      oblivious_http_request.EncapsulateAndSerialize();
  auto request = std::make_unique<ObliviousGetValuesRequest>();
  google::api::HttpBody body;
  PS_VLOG(7) << "Sending encrypted request: "
             << absl::BytesToHexString(encrypted_request);
  *body.mutable_data() = std::move(encrypted_request);
  *request->mutable_raw_body() = std::move(body);
  auto params = std::make_unique<RawClientParams<
      ObliviousGetValuesRequest, google::api::HttpBody, GetValuesResponse>>(
      std::move(request), std::move(on_done));
  context->set_deadline(GetClientContextDeadline(timeout, kMaxClientTimeout));
  context->AddMetadata(std::string(kKVContentTypeHeader),
                       std::string(kContentEncodingProtoHeaderValue));
  PS_VLOG(5) << "Sending RPC ...";
  SendRpc(std::make_unique<quiche::ObliviousHttpRequest::Context>(
              std::move(oblivious_http_request).ReleaseContext()),
          context, params.release());
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
