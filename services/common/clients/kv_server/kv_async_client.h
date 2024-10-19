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

#ifndef SERVICES_COMMON_CLIENTS_KV_SERVER_KV_ASYNC_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_KV_SERVER_KV_ASYNC_CLIENT_H_

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/functional/any_invocable.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "google/protobuf/util/json_util.h"
#include "public/constants.h"
#include "public/query/cpp/client_utils.h"
#include "public/query/v2/get_values_v2.grpc.pb.h"
#include "public/query/v2/get_values_v2.pb.h"
#include "quiche/binary_http/binary_http_message.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "services/common/clients/async_client.h"
#include "services/common/clients/client_params.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/oblivious_http_utils.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/encryption/key_fetcher/key_fetcher_utils.h"
#include "src/include/openssl/hpke.h"
#include "src/util/status_macro/status_macros.h"

using google::protobuf::util::JsonStringToMessage;

namespace privacy_sandbox::bidding_auction_servers {

using kv_server::v2::GetValuesRequest;
using kv_server::v2::GetValuesResponse;
using kv_server::v2::ObliviousGetValuesRequest;
using ObliviousHttpRequestUptr =
    std::unique_ptr<quiche::ObliviousHttpRequest::Context>;
using KVAsyncClient =
    AsyncClient<ObliviousGetValuesRequest, google::api::HttpBody,
                GetValuesRequest, GetValuesResponse>;

constexpr absl::Duration kMaxTimeout = absl::Milliseconds(60000);

// This class is an async grpc client for KV Service.
class KVAsyncGrpcClient
    : public AsyncClient<ObliviousGetValuesRequest, google::api::HttpBody,
                         GetValuesRequest, GetValuesResponse> {
 public:
  KVAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      std::unique_ptr<kv_server::v2::KeyValueService::Stub> stub);

  absl::Status ExecuteInternal(
      std::unique_ptr<GetValuesRequest> raw_request,
      grpc::ClientContext* context,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
               ResponseMetadata) &&>
          on_done,
      absl::Duration timeout = kMaxTimeout,
      RequestConfig request_config = {}) override;

 protected:
  void SendRpc(ObliviousHttpRequestUptr oblivious_http_context,
               grpc::ClientContext* context,
               RawClientParams<ObliviousGetValuesRequest, google::api::HttpBody,
                               GetValuesResponse>* params) const;

  server_common::KeyFetcherManagerInterface& key_fetcher_manager_;
  std::unique_ptr<kv_server::v2::KeyValueService::Stub> stub_;
  server_common::CloudPlatform cloud_platform_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_KV_SERVER_KV_ASYNC_CLIENT_H_
