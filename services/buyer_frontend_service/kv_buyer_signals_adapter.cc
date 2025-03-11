//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "kv_buyer_signals_adapter.h"

#include <utility>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/common/clients/kv_server/kv_v2.h"
#include "services/common/util/json_util.h"
#include "src/util/status_macro/status_macros.h"

// TODO(b/369379352): Move under //services/buyer_frontend_service/providers
namespace privacy_sandbox::bidding_auction_servers {

using kv_server::UDFArgument;
using std::vector;

namespace {
inline constexpr char kKeys[] = "keys";
inline constexpr char kInterestGroupNames[] = "interestGroupNames";
inline constexpr char kClient[] = "Bna.PA.Buyer.20240930";
inline constexpr char kHostname[] = "hostname";
inline constexpr char kClientType[] = "client_type";
inline constexpr char kExperimentGroupId[] = "experiment_group_id";
inline constexpr char kBuyerSignals[] = "buyer_signals";
inline constexpr char kByosOutput[] = "byos_output";

kv_server::v2::GetValuesRequest GetRequest(
    const GetBidsRequest::GetBidsRawRequest& get_bids_raw_request) {
  kv_server::v2::GetValuesRequest req;
  req.set_client_version(kClient);
  auto& metadata = *(req.mutable_metadata()->mutable_fields());
  (metadata)[kHostname].set_string_value(get_bids_raw_request.publisher_name());
  (metadata)[kClientType].set_string_value(
      absl::StrCat(get_bids_raw_request.client_type()));
  if (get_bids_raw_request.has_buyer_kv_experiment_group_id()) {
    (metadata)[kExperimentGroupId].set_string_value(
        absl::StrCat(get_bids_raw_request.buyer_kv_experiment_group_id()));
  }
  return req;
}

UDFArgument BuildArgument(std::string tag, vector<std::string> keys) {
  UDFArgument arg;
  arg.mutable_tags()->add_values()->set_string_value(std::move(tag));
  auto* key_list = arg.mutable_data()->mutable_list_value();
  for (auto& key : keys) {
    key_list->add_values()->set_string_value(std::move(key));
  }
  return arg;
}

absl::Status ValidateInterestGroups(const BuyerInputForBidding& buyer_input) {
  if (buyer_input.interest_groups().empty()) {
    return absl::InvalidArgumentError("No interest groups in the buyer input");
  }
  for (auto& ig : buyer_input.interest_groups()) {
    if (!ig.bidding_signals_keys().empty()) {
      return absl::OkStatus();
    }
  }
  return absl::InvalidArgumentError(
      "Interest groups don't have any bidding signals");
}

}  // namespace

absl::StatusOr<std::unique_ptr<BiddingSignals>> ConvertV2BiddingSignalsToV1(
    std::unique_ptr<kv_server::v2::GetValuesResponse> response,
    KVV2AdapterStats& v2_adapter_stats) {
  PS_ASSIGN_OR_RETURN(auto trusted_signals, ConvertKvV2ResponseToV1String(
                                                {kKeys, kInterestGroupNames},
                                                *response, v2_adapter_stats));
  return std::make_unique<BiddingSignals>(BiddingSignals{
      std::make_unique<std::string>(std::move(trusted_signals))});
}

absl::StatusOr<std::unique_ptr<kv_server::v2::GetValuesRequest>>
CreateV2BiddingRequest(const BiddingSignalsRequest& bidding_signals_request,
                       bool propagate_buyer_signals_to_tkv,
                       std::unique_ptr<std::string> byos_output) {
  auto& bids_request = bidding_signals_request.get_bids_raw_request_;
  PS_RETURN_IF_ERROR(
      ValidateInterestGroups(bids_request.buyer_input_for_bidding()));
  std::unique_ptr<kv_server::v2::GetValuesRequest> req =
      std::make_unique<kv_server::v2::GetValuesRequest>(
          GetRequest(bids_request));
  {
    *req->mutable_consented_debug_config() =
        bids_request.consented_debug_config();
  }
  { *req->mutable_log_context() = bids_request.log_context(); }
  if (propagate_buyer_signals_to_tkv) {
    (*req->mutable_metadata()->mutable_fields())[kBuyerSignals]
        .set_string_value(bids_request.buyer_signals());
  }
  if (byos_output != nullptr) {
    (*req->mutable_metadata()->mutable_fields())[kByosOutput].set_string_value(
        std::move(*byos_output));
  }
  int compression_and_partition_id = 0;
  // TODO (b/369181315): this needs to be reworked to include multiple IGs's
  // keys per partition.
  for (auto& ig : bids_request.buyer_input_for_bidding().interest_groups()) {
    kv_server::v2::RequestPartition* partition = req->add_partitions();
    *partition->add_arguments() =
        BuildArgument(kInterestGroupNames, {ig.name()});
    for (auto& key : ig.bidding_signals_keys()) {
      std::vector<std::string> keys;
      keys.push_back(key);
      *partition->add_arguments() = BuildArgument(kKeys, std::move(keys));
      partition->set_id(compression_and_partition_id);
      partition->set_compression_group_id(compression_and_partition_id);
    }
    compression_and_partition_id++;
  }
  PS_VLOG(8) << __func__
             << " Created KV lookup request: " << req->DebugString();
  return req;
}

}  // namespace privacy_sandbox::bidding_auction_servers
