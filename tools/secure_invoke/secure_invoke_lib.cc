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

#include "tools/secure_invoke/secure_invoke_lib.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <utility>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include "absl/container/btree_map.h"
#include "absl/flags/flag.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "services/common/clients/async_grpc/grpc_client_utils.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"
#include "services/common/clients/seller_frontend_server/seller_frontend_async_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/util/json_util.h"
#include "src/encryption/key_fetcher/fake_key_fetcher_manager.h"
#include "tools/secure_invoke/flags.h"
#include "tools/secure_invoke/payload_generator/payload_packaging.h"
#include "tools/secure_invoke/payload_generator/payload_packaging_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

constexpr char kJsonFormat[] = "JSON";

absl::StatusOr<rapidjson::Document> SortAuctionResultBiddingGroups(
    absl::string_view auction_result_json) {
  PS_ASSIGN_OR_RETURN(rapidjson::Document d,
                      ParseJsonString(auction_result_json));

  std::string key_name = "biddingGroups";
  absl::btree_map<std::string, rapidjson::Value> sorted_map;
  if (d.HasMember(key_name.c_str())) {
    for (auto& m : d[key_name.c_str()].GetObject()) {
      sorted_map[m.name.GetString()] = m.value.GetObject();
    }
  }

  d.RemoveMember(key_name.c_str());  // remove old unsorted map

  rapidjson::Value sorted_val(rapidjson::kObjectType);

  for (auto& m : sorted_map) {
    rapidjson::Value key(m.first.c_str(), d.GetAllocator());
    sorted_val.AddMember(key, m.second, d.GetAllocator());
  }

  d.AddMember(rapidjson::Value().SetString(key_name.c_str(), d.GetAllocator()),
              sorted_val, d.GetAllocator());  // add new sorted map

  return d;
}

absl::StatusOr<std::string> ParseSelectAdResponse(
    std::unique_ptr<SelectAdResponse> resp, ClientType client_type,
    quiche::ObliviousHttpRequest::Context& context, const HpkeKeyset& keyset) {
  absl::StatusOr<AuctionResult> res;
  // Server component Auction
  if (!resp->key_id().empty()) {
    res = UnpackageResultForServerComponentAuction(
        *resp->mutable_auction_result_ciphertext(), resp->key_id(), keyset);
  } else {
    res = UnpackageAuctionResult(*resp->mutable_auction_result_ciphertext(),
                                 client_type, context, keyset);
  }

  if (!res.ok()) {
    return res.status();
  }
  std::string auction_result_json;
  auto auction_result_json_status =
      google::protobuf::util::MessageToJsonString(*res, &auction_result_json);
  if (!auction_result_json_status.ok()) {
    return auction_result_json_status;
  }
  // Sort bidding groups for easy comparison.
  PS_ASSIGN_OR_RETURN(rapidjson::Document auction_result_doc,
                      SortAuctionResultBiddingGroups(auction_result_json));
  if (!resp->has_debug_info()) {
    return SerializeJsonDoc(auction_result_doc);
  }

  std::string debug_info_json;
  CHECK_OK(google::protobuf::util::MessageToJsonString(resp->debug_info(),
                                                       &debug_info_json));
  absl::StatusOr<rapidjson::Document> debug_info_document =
      ParseJsonString(debug_info_json);
  CHECK_OK(debug_info_document);
  rapidjson::Document output_doc;
  output_doc.SetObject();
  rapidjson::Document::AllocatorType& allocator = output_doc.GetAllocator();
  output_doc.AddMember("auctionResult", std::move(auction_result_doc),
                       allocator);
  output_doc.AddMember("debugInfo", std::move(*debug_info_document), allocator);
  absl::StatusOr<std::string> json_output = SerializeJsonDoc(output_doc);
  CHECK_OK(json_output);
  return *json_output;
}
}  // namespace

absl::Status InvokeSellerFrontEndWithRawRequest(
    absl::string_view raw_select_ad_request_json,
    const RequestOptions& request_options, ClientType client_type,
    const HpkeKeyset& keyset, bool enable_debug_reporting,
    std::optional<bool> enable_debug_info,
    std::optional<bool> enable_unlimited_egress,
    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&> on_done) {
  // Validate input
  if (request_options.host_addr.empty()) {
    return absl::InvalidArgumentError("SFE host address must be specified");
  }

  if (request_options.client_ip.empty()) {
    return absl::InvalidArgumentError("Client IP must be specified");
  }

  if (request_options.user_agent.empty()) {
    return absl::InvalidArgumentError("User Agent must be specified");
  }

  if (request_options.accept_language.empty()) {
    return absl::InvalidArgumentError("Accept Language must be specified");
  }

  // Package request.
  std::pair<std::unique_ptr<SelectAdRequest>,
            quiche::ObliviousHttpRequest::Context>
      request_context_pair = PackagePlainTextSelectAdRequest(
          raw_select_ad_request_json, client_type, keyset,
          enable_debug_reporting, enable_debug_info,
          absl::GetFlag(FLAGS_pas_buyer_input_json), enable_unlimited_egress);

  // Add request headers.
  RequestMetadata request_metadata;
  request_metadata.emplace("x-bna-client-ip", request_options.client_ip);
  request_metadata.emplace("x-user-agent", request_options.user_agent);
  request_metadata.emplace("x-accept-language",
                           request_options.accept_language);

  // Create client.
  SellerFrontEndServiceClientConfig service_client_config;
  service_client_config.server_addr = request_options.host_addr;
  service_client_config.secure_client = !request_options.insecure;
  SellerFrontEndGrpcClient sfe_client(service_client_config);

  return sfe_client.Execute(
      std::move(request_context_pair.first), request_metadata,
      [context = std::move(request_context_pair.second),
       onDone = std::move(on_done), client_type,
       keyset](absl::StatusOr<std::unique_ptr<SelectAdResponse>> resp) mutable {
        if (resp.ok()) {
          std::move(onDone)(ParseSelectAdResponse(
              std::move(resp.value()), client_type, context, keyset));
        } else {
          std::move(onDone)(resp.status());
        }
      },
      absl::Duration(timeout));
}

absl::Status InvokeBuyerFrontEndWithRawRequest(
    const GetBidsRequest::GetBidsRawRequest& get_bids_raw_request,
    const RequestOptions& request_options, const HpkeKeyset& keyset,
    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&> on_done,
    std::unique_ptr<BuyerFrontEnd::StubInterface> stub = nullptr) {
  // Validate input
  if (request_options.host_addr.empty()) {
    return absl::InvalidArgumentError("BFE host address must be specified");
  }

  if (request_options.client_ip.empty()) {
    return absl::InvalidArgumentError("Client IP must be specified");
  }

  if (request_options.user_agent.empty()) {
    return absl::InvalidArgumentError("User Agent must be specified");
  }

  if (request_options.accept_language.empty()) {
    return absl::InvalidArgumentError("Accept Language must be specified");
  }

  // Add request headers.
  RequestMetadata request_metadata;
  request_metadata.emplace("x-bna-client-ip", request_options.client_ip);
  request_metadata.emplace("x-user-agent", request_options.user_agent);
  request_metadata.emplace("x-accept-language",
                           request_options.accept_language);

  // Create service client.
  BuyerServiceClientConfig service_client_config = {
      .server_addr = request_options.host_addr,
      .secure_client = !request_options.insecure,
  };
  auto key_fetcher_manager =
      std::make_unique<server_common::FakeKeyFetcherManager>(
          keyset.public_key, "unused", std::to_string(keyset.key_id));
  auto crypto_client = CreateCryptoClient();
  BuyerFrontEndAsyncGrpcClient bfe_client(
      key_fetcher_manager.get(), crypto_client.get(), service_client_config,
      std::move(stub));
  absl::Notification notification;

  grpc::ClientContext context;
  for (const auto& it : request_metadata) {
    context.AddMetadata(it.first, it.second);
  }

  auto call_status = bfe_client.ExecuteInternal(
      std::make_unique<GetBidsRequest::GetBidsRawRequest>(get_bids_raw_request),
      &context,
      [onDone = std::move(on_done), &notification, start = absl::Now()](
          absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>
              raw_response,
          ResponseMetadata response_metadata) mutable {
        PS_VLOG(1) << "Received bid response from BFE in "
                   << ((absl::Now() - start) / absl::Milliseconds(1)) << " ms.";
        if (!raw_response.ok()) {
          std::move(onDone)(raw_response.status());
        } else {
          std::string response;
          auto response_status = google::protobuf::util::MessageToJsonString(
              **raw_response, &response);
          if (!response_status.ok()) {
            std::move(onDone)(absl::InternalError(
                "Failed to convert the server response to JSON string"));
          } else {
            std::move(onDone)(std::move(response));
          }
        }
        notification.Notify();
      },
      absl::Duration(timeout));
  CHECK(call_status.ok()) << call_status;
  notification.WaitForNotification();
  return call_status;
}

std::string LoadFile(absl::string_view file_path) {
  std::ifstream ifs(file_path.data());
  return std::string((std::istreambuf_iterator<char>(ifs)),
                     (std::istreambuf_iterator<char>()));
}

absl::Status SendRequestToSfe(ClientType client_type, const HpkeKeyset& keyset,
                              bool enable_debug_reporting,
                              std::optional<bool> enable_debug_info,
                              std::optional<bool> enable_unlimited_egress) {
  std::string raw_select_ad_request_json = absl::GetFlag(FLAGS_json_input_str);
  if (raw_select_ad_request_json.empty()) {
    raw_select_ad_request_json = LoadFile(absl::GetFlag(FLAGS_input_file));
  }
  privacy_sandbox::bidding_auction_servers::RequestOptions options;
  options.host_addr = absl::GetFlag(FLAGS_host_addr);
  options.client_ip = absl::GetFlag(FLAGS_client_ip);
  options.user_agent = absl::GetFlag(FLAGS_client_user_agent);
  options.accept_language = absl::GetFlag(FLAGS_client_accept_language);
  options.insecure = absl::GetFlag(FLAGS_insecure);
  absl::Notification notification;
  absl::Status status = privacy_sandbox::bidding_auction_servers::
      InvokeSellerFrontEndWithRawRequest(
          raw_select_ad_request_json, options, client_type, keyset,
          enable_debug_reporting, enable_debug_info, enable_unlimited_egress,
          [&notification](absl::StatusOr<std::string> output) {
            if (output.ok()) {
              // Standard output to compare response
              // programatically by utilities.
              std::cout << *output;
            } else {
              std::cerr << output.status();
            }
            notification.Notify();
          });
  CHECK(status.ok()) << status;
  notification.WaitForNotification();
  return status;
}

GetBidsRequest::GetBidsRawRequest GetBidsRawRequestFromInput(
    bool enable_debug_reporting, std::optional<bool> enable_unlimited_egress) {
  std::string raw_get_bids_request_str = absl::GetFlag(FLAGS_json_input_str);
  const bool is_json = (!raw_get_bids_request_str.empty() ||
                        absl::GetFlag(FLAGS_input_format) == kJsonFormat);
  GetBidsRequest::GetBidsRawRequest get_bids_raw_request;
  get_bids_raw_request.set_enable_debug_reporting(enable_debug_reporting);
  if (enable_unlimited_egress) {
    get_bids_raw_request.set_enable_unlimited_egress(*enable_unlimited_egress);
  }
  if (is_json) {
    if (raw_get_bids_request_str.empty()) {
      raw_get_bids_request_str = LoadFile(absl::GetFlag(FLAGS_input_file));
    }
    auto result = google::protobuf::util::JsonStringToMessage(
        raw_get_bids_request_str, &get_bids_raw_request);
    CHECK(result.ok())
        << "Failed to convert the provided raw request JSON to proto "
        << "(Is the input malformed?). Input:\n"
        << raw_get_bids_request_str << "\nError:\n:" << result;
  } else {
    raw_get_bids_request_str = LoadFile(absl::GetFlag(FLAGS_input_file));
    CHECK(google::protobuf::TextFormat::ParseFromString(
        raw_get_bids_request_str, &get_bids_raw_request))
        << "Failed to create proto object from the input file. Input:\n"
        << raw_get_bids_request_str;
  }
  return get_bids_raw_request;
}

std::string PackagePlainTextGetBidsRequestToJson(
    const HpkeKeyset& keyset, bool enable_debug_reporting,
    std::optional<bool> enable_unlimited_egress) {
  GetBidsRequest::GetBidsRawRequest get_bids_raw_request =
      GetBidsRawRequestFromInput(enable_debug_reporting,
                                 enable_unlimited_egress);
  auto key_fetcher_manager =
      std::make_unique<server_common::FakeKeyFetcherManager>(
          keyset.public_key, "unused", std::to_string(keyset.key_id));
  auto crypto_client = CreateCryptoClient();
  auto secret_request = EncryptRequestWithHpke<GetBidsRequest>(
      get_bids_raw_request.SerializeAsString(), *crypto_client,
      *key_fetcher_manager, server_common::CloudPlatform::kGcp);
  CHECK(secret_request.ok()) << secret_request.status();
  std::string get_bids_request_json;
  auto get_bids_request_json_status =
      google::protobuf::util::MessageToJsonString(*secret_request->second,
                                                  &get_bids_request_json);
  CHECK(get_bids_request_json_status.ok()) << get_bids_request_json_status;
  return get_bids_request_json;
}

absl::Status SendRequestToBfe(
    const HpkeKeyset& keyset, bool enable_debug_reporting,
    std::unique_ptr<BuyerFrontEnd::StubInterface> stub,
    std::optional<bool> enable_unlimited_egress) {
  GetBidsRequest::GetBidsRawRequest get_bids_raw_request =
      GetBidsRawRequestFromInput(enable_debug_reporting,
                                 enable_unlimited_egress);
  privacy_sandbox::bidding_auction_servers::RequestOptions request_options;
  request_options.host_addr = absl::GetFlag(FLAGS_host_addr);
  request_options.client_ip = absl::GetFlag(FLAGS_client_ip);
  request_options.user_agent = absl::GetFlag(FLAGS_client_user_agent);
  request_options.accept_language = absl::GetFlag(FLAGS_client_accept_language);
  request_options.insecure = absl::GetFlag(FLAGS_insecure);
  absl::Status status = absl::OkStatus();
  auto call_status = privacy_sandbox::bidding_auction_servers::
      InvokeBuyerFrontEndWithRawRequest(
          get_bids_raw_request, request_options, keyset,
          [&status](absl::StatusOr<std::string> output) {
            if (output.ok()) {
              // Standard output to compare response
              // programatically by utilities.
              std::cout << *output;
            } else {
              status = output.status();
              std::cerr << output.status();
            }
          },
          std::move(stub));
  CHECK(call_status.ok()) << call_status;
  return status;
}

}  // namespace privacy_sandbox::bidding_auction_servers
