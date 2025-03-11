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

#include "kv_seller_signals_adapter.h"

#include <string>
#include <utility>

#include "services/common/clients/kv_server/kv_v2.h"
#include "services/common/constants/common_constants.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using kv_server::UDFArgument;
using kv_server::v2::GetValuesRequest;
using kv_server::v2::GetValuesResponse;

inline constexpr char kClientType[] = "client_type";

GetValuesRequest GetEmptyRequestWithMetadata(
    const ScoringSignalsRequest& scoring_signals_request) {
  GetValuesRequest request;
  request.set_client_version(kSellerKvClientVersion);
  auto& metadata = *(request.mutable_metadata()->mutable_fields());
  (metadata)[kClientType].set_string_value(
      absl::StrCat(scoring_signals_request.client_type_));
  (metadata)[kKvExperimentGroupId].set_string_value(
      std::string(scoring_signals_request.seller_kv_experiment_group_id_));
  return request;
}

UDFArgument BuildArgument(std::string_view key, std::string_view tag) {
  UDFArgument arg;
  arg.mutable_tags()->add_values()->set_string_value(std::string(tag));
  if (!key.empty()) {
    arg.mutable_data()->mutable_list_value()->add_values()->set_string_value(
        std::string(key));
  }
  return arg;
}

UDFArgument BuildArgument(
    const google::protobuf::RepeatedPtrField<std::string>& keys,
    std::string_view tag) {
  UDFArgument arg;
  arg.mutable_tags()->add_values()->set_string_value(std::string(tag));
  auto* key_list = arg.mutable_data()->mutable_list_value();
  for (auto& key : keys) {
    if (!key.empty()) {
      key_list->add_values()->set_string_value(key);
    }
  }
  return arg;
}

}  // namespace

absl::StatusOr<std::unique_ptr<GetValuesRequest>> CreateV2ScoringRequest(
    const ScoringSignalsRequest& scoring_signals_request, bool is_pas_enabled,
    std::optional<server_common::ConsentedDebugConfiguration>
        consented_debug_config) {
  std::unique_ptr<kv_server::v2::GetValuesRequest> request =
      std::make_unique<kv_server::v2::GetValuesRequest>(
          GetEmptyRequestWithMetadata(scoring_signals_request));
  if (consented_debug_config) {
    *request->mutable_consented_debug_config() =
        std::move(*consented_debug_config);
  }
  // TODO(b/370527752): Add metadata
  int compression_and_partition_id = 0;
  for (const auto& [unused_buyer, get_bids_response] :
       scoring_signals_request.buyer_bids_map_) {
    for (const auto& ad : get_bids_response->bids()) {
      kv_server::v2::RequestPartition partition;
      UDFArgument render_urls_arg = BuildArgument(ad.render(), kKvRenderUrls);
      UDFArgument ad_component_render_urls_arg =
          BuildArgument(ad.ad_components(), kKvAdComponentRenderUrls);
      if (!render_urls_arg.data().list_value().values().empty()) {
        *partition.add_arguments() = std::move(render_urls_arg);
      }
      if (!ad_component_render_urls_arg.data().list_value().values().empty()) {
        *partition.add_arguments() = std::move(ad_component_render_urls_arg);
      }
      if (!partition.arguments().empty()) {
        partition.set_id(compression_and_partition_id);
        partition.set_compression_group_id(compression_and_partition_id);
        *request->add_partitions() = std::move(partition);
        compression_and_partition_id++;
      }
    }
    if (!is_pas_enabled) {
      continue;
    }
    for (const auto& ad : get_bids_response->protected_app_signals_bids()) {
      kv_server::v2::RequestPartition partition;
      UDFArgument render_urls_arg = BuildArgument(ad.render(), kKvRenderUrls);
      if (!render_urls_arg.data().list_value().values().empty()) {
        *partition.add_arguments() = std::move(render_urls_arg);
        partition.set_id(compression_and_partition_id);
        partition.set_compression_group_id(compression_and_partition_id);
        *request->add_partitions() = std::move(partition);
        compression_and_partition_id++;
      }
    }
  }

  if (request->partitions().empty()) {
    return absl::InvalidArgumentError(
        "No renderUrls and adComponentRenderUrls in scoring_signals_request");
  }
  return request;
}

absl::StatusOr<std::unique_ptr<ScoringSignals>>
ConvertV2ResponseToV1ScoringSignals(std::unique_ptr<GetValuesResponse> response,
                                    KVV2AdapterStats& v2_adapter_stats) {
  // TODO(b/374748369): Add data version support.
  PS_ASSIGN_OR_RETURN(
      auto scoring_signals,
      ConvertKvV2ResponseToV1String({kKvRenderUrls, kKvAdComponentRenderUrls},
                                    *response, v2_adapter_stats));
  return std::make_unique<ScoringSignals>(ScoringSignals{
      std::make_unique<std::string>(std::move(scoring_signals))});
}
}  // namespace privacy_sandbox::bidding_auction_servers
